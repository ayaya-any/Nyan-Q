#include "core/ProfileStore.h"

#include "app/AppPaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrlQuery>
#include <QUuid>

#include <utility>

namespace {
constexpr qint64 kMaxProfileSize = 16LL * 1024 * 1024;

QJsonObject profileToJson(const cq::ProfileMeta& p)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), p.id);
    o.insert(QStringLiteral("name"), p.name);
    o.insert(QStringLiteral("type"), p.type);
    o.insert(QStringLiteral("url"), p.url);
    o.insert(QStringLiteral("fileName"), p.fileName);
    o.insert(QStringLiteral("updatedAt"), p.updatedAt.toUTC().toString(Qt::ISODateWithMs));
    o.insert(QStringLiteral("upload"), static_cast<double>(p.upload));
    o.insert(QStringLiteral("download"), static_cast<double>(p.download));
    o.insert(QStringLiteral("total"), static_cast<double>(p.total));
    o.insert(QStringLiteral("expireAt"), static_cast<double>(p.expireAt));
    o.insert(QStringLiteral("updateIntervalHours"), p.updateIntervalHours);
    return o;
}

cq::ProfileMeta profileFromJson(const QJsonObject& o)
{
    cq::ProfileMeta p;
    p.id = o.value(QStringLiteral("id")).toString();
    p.name = o.value(QStringLiteral("name")).toString();
    p.type = o.value(QStringLiteral("type")).toString();
    p.url = o.value(QStringLiteral("url")).toString();
    p.fileName = o.value(QStringLiteral("fileName")).toString();
    p.updatedAt = QDateTime::fromString(o.value(QStringLiteral("updatedAt")).toString(),
                                        Qt::ISODateWithMs);
    p.upload = static_cast<qint64>(o.value(QStringLiteral("upload")).toDouble());
    p.download = static_cast<qint64>(o.value(QStringLiteral("download")).toDouble());
    p.total = static_cast<qint64>(o.value(QStringLiteral("total")).toDouble());
    p.expireAt = static_cast<qint64>(o.value(QStringLiteral("expireAt")).toDouble());
    p.updateIntervalHours = o.value(QStringLiteral("updateIntervalHours")).toInt();
    return p;
}

QString normalizedName(const QString& raw)
{
    QString name = raw.trimmed();
    name.remove(QLatin1Char('\r'));
    name.remove(QLatin1Char('\n'));
    if (name.size() > 80)
        name = name.left(80);
    return name;
}
}  // namespace

ProfileStore::ProfileStore(QObject* parent)
    : QObject(parent)
{
}

ProfileStore::~ProfileStore() = default;

void ProfileStore::initialize(const QString& profilesDir)
{
    m_directory = QDir::cleanPath(profilesDir);
    QDir().mkpath(m_directory);
    loadMetadata();
    ensureBuiltin();
    ensureActive();
}

QByteArray ProfileStore::userAgent()
{
    return QByteArrayLiteral("clash-verge/v2.0.3");
}

void ProfileStore::loadMetadata()
{
    m_profiles.clear();
    m_activeId.clear();

    QFile file(QDir(m_directory).filePath(QStringLiteral("profiles.json")));
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;
    const QJsonObject root = doc.object();
    m_activeId = root.value(QStringLiteral("active")).toString();
    const QJsonArray list = root.value(QStringLiteral("profiles")).toArray();
    for (const QJsonValue& value : list) {
        if (!value.isObject())
            continue;
        cq::ProfileMeta profile = profileFromJson(value.toObject());
        if (profile.id.isEmpty() || profile.fileName.isEmpty())
            continue;
        if (!QFileInfo::exists(configPath(profile)))
            continue;
        profile.active = profile.id == m_activeId;
        m_profiles.append(profile);
    }
}

void ProfileStore::saveMetadata() const
{
    QJsonArray list;
    for (const cq::ProfileMeta& profile : m_profiles)
        list.append(profileToJson(profile));

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("active"), m_activeId);
    root.insert(QStringLiteral("profiles"), list);

    QSaveFile file(QDir(m_directory).filePath(QStringLiteral("profiles.json")));
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.commit();
}

void ProfileStore::ensureBuiltin()
{
    if (!m_profiles.isEmpty())
        return;

    QFile resource(QStringLiteral(":/config/starter.yaml"));
    if (!resource.open(QIODevice::ReadOnly))
        return;
    const QByteArray data = resource.readAll();

    cq::ProfileMeta profile;
    profile.id = makeId();
    profile.name = QStringLiteral("内置 · 直连");
    profile.type = QStringLiteral("builtin");
    profile.fileName = QStringLiteral("builtin-%1.yaml").arg(profile.id.left(6));
    profile.updatedAt = QDateTime::currentDateTime();
    if (!writeProfileFile(profile.fileName, data, nullptr))
        return;

    m_profiles.append(profile);
    m_activeId = profile.id;
    saveMetadata();
}

void ProfileStore::ensureActive()
{
    if (m_profiles.isEmpty()) {
        ensureBuiltin();
        return;
    }

    bool found = false;
    for (const cq::ProfileMeta& p : m_profiles) {
        if (p.id == m_activeId) {
            found = true;
            break;
        }
    }
    if (!found) {
        m_activeId = m_profiles.first().id;
        saveMetadata();
    }
}

QVector<cq::ProfileMeta> ProfileStore::profiles() const
{
    QVector<cq::ProfileMeta> result = m_profiles;
    for (auto& profile : result)
        profile.active = profile.id == m_activeId;
    return result;
}

QString ProfileStore::activeId() const
{
    return m_activeId;
}

std::optional<cq::ProfileMeta> ProfileStore::activeProfile() const
{
    return find(m_activeId);
}

std::optional<cq::ProfileMeta> ProfileStore::find(const QString& id) const
{
    for (const cq::ProfileMeta& profile : m_profiles) {
        if (profile.id == id) {
            cq::ProfileMeta copy = profile;
            copy.active = profile.id == m_activeId;
            return copy;
        }
    }
    return std::nullopt;
}

QString ProfileStore::configPath(const cq::ProfileMeta& profile) const
{
    return QDir(m_directory).filePath(profile.fileName);
}

QByteArray ProfileStore::configContent(const QString& id, QString* error) const
{
    const auto profile = find(id);
    if (!profile) {
        if (error)
            *error = QStringLiteral("找不到这个配置");
        return {};
    }

    QFile file(configPath(*profile));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取配置文件：%1").arg(file.errorString());
        return {};
    }
    return file.readAll();
}

bool ProfileStore::saveConfigContent(const QString& id, const QByteArray& content, QString* error)
{
    if (m_busy) {
        if (error)
            *error = QStringLiteral("订阅任务进行中，暂时无法保存配置");
        return false;
    }

    int index = -1;
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles.at(i).id == id) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        if (error)
            *error = QStringLiteral("找不到这个配置");
        return false;
    }

    QString validationError;
    if (!validateContent(content, &validationError)) {
        if (error)
            *error = validationError;
        return false;
    }
    if (content.size() > kMaxProfileSize) {
        if (error)
            *error = QStringLiteral("配置文件超过 16 MB，无法保存");
        return false;
    }

    cq::ProfileMeta& profile = m_profiles[index];
    if (!writeProfileFile(profile.fileName, content, error))
        return false;

    profile.updatedAt = QDateTime::currentDateTime();
    if (profile.type == QStringLiteral("builtin"))
        profile.type = QStringLiteral("local");
    saveMetadata();

    emit changed();
    emit profileUpdated(id);
    emit operationSucceeded(QStringLiteral("配置「%1」已保存").arg(profile.name));
    return true;
}

bool ProfileStore::renameProfile(const QString& id, const QString& name, QString* error)
{
    if (error)
        error->clear();
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        if (error)
            *error = QStringLiteral("配置名称不能为空");
        return false;
    }
    if (trimmed.size() > 80) {
        if (error)
            *error = QStringLiteral("配置名称不能超过 80 个字符");
        return false;
    }

    int index = -1;
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles.at(i).id == id) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        if (error)
            *error = QStringLiteral("找不到这个配置");
        return false;
    }
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (i != index && m_profiles.at(i).name.compare(trimmed, Qt::CaseInsensitive) == 0) {
            if (error)
                *error = QStringLiteral("已存在同名配置");
            return false;
        }
    }

    cq::ProfileMeta& profile = m_profiles[index];
    if (profile.name == trimmed)
        return true;
    profile.name = trimmed;
    saveMetadata();
    emit changed();
    emit profileUpdated(id);
    if (m_activeId == id)
        emit activeChanged(id);
    emit operationSucceeded(QStringLiteral("配置名称已更新"));
    return true;
}

QString ProfileStore::makeId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
}

QString ProfileStore::fileNameForId(const QString& id) const
{
    return QStringLiteral("profile-%1.yaml").arg(id);
}

QString ProfileStore::uniqueName(const QString& desired) const
{
    QString base = normalizedName(desired);
    if (base.isEmpty())
        base = QStringLiteral("未命名配置");
    QString candidate = base;
    int index = 2;
    bool clash = true;
    while (clash) {
        clash = false;
        for (const cq::ProfileMeta& profile : m_profiles) {
            if (profile.name.compare(candidate, Qt::CaseInsensitive) == 0) {
                candidate = QStringLiteral("%1 (%2)").arg(base).arg(index++);
                clash = true;
                break;
            }
        }
    }
    return candidate;
}

bool ProfileStore::writeProfileFile(const QString& fileName, const QByteArray& data,
                                    QString* error) const
{
    QDir().mkpath(m_directory);
    QSaveFile file(QDir(m_directory).filePath(fileName));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入配置目录");
        return false;
    }
    if (file.write(data) != data.size()) {
        if (error)
            *error = QStringLiteral("写入配置文件失败");
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("保存配置文件失败");
        return false;
    }
    return true;
}

bool ProfileStore::validateContent(const QByteArray& data, QString* error) const
{
    if (data.isEmpty()) {
        if (error)
            *error = QStringLiteral("订阅内容为空");
        return false;
    }
    if (data.size() > kMaxProfileSize) {
        if (error)
            *error = QStringLiteral("配置文件超过 16 MB，已拒绝导入");
        return false;
    }

    const QString text = QString::fromUtf8(data);
    if (text.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("订阅内容不是有效的 UTF-8 文本");
        return false;
    }
    if (text.trimmed().startsWith(QLatin1Char('<'))) {
        if (error)
            *error = QStringLiteral("服务器返回了网页而不是 Clash 配置，请检查订阅链接");
        return false;
    }

    static const QRegularExpression marker(
        QStringLiteral(R"((?m)^\s*(proxies|proxy-groups|proxy-providers|rules|mixed-port|port)\s*:)"));
    if (!text.contains(marker)) {
        if (error) {
            *error = QStringLiteral("没有识别到 Clash / Mihomo YAML 配置。"
                                    "请使用 Clash 格式的订阅，而不是 Base64 节点列表。");
        }
        return false;
    }
    return true;
}

QString ProfileStore::parseProfileTitle(const QByteArray& value)
{
    QString title = QString::fromUtf8(value).trimmed();
    if (title.startsWith(QStringLiteral("base64:"), Qt::CaseInsensitive)) {
        const QByteArray decoded = QByteArray::fromBase64(title.mid(7).toLatin1());
        const QString decodedTitle = QString::fromUtf8(decoded).trimmed();
        if (!decodedTitle.isEmpty())
            title = decodedTitle;
    }
    return normalizedName(title);
}

void ProfileStore::applyUserInfo(const QByteArray& value, cq::ProfileMeta& profile)
{
    const QString text = QString::fromLatin1(value);
    const QStringList parts = text.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const int equals = part.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        const QString key = part.left(equals).trimmed().toLower();
        bool ok = false;
        const qint64 number = part.mid(equals + 1).trimmed().toLongLong(&ok);
        if (!ok)
            continue;
        if (key == QStringLiteral("upload"))
            profile.upload = number;
        else if (key == QStringLiteral("download"))
            profile.download = number;
        else if (key == QStringLiteral("total"))
            profile.total = number;
        else if (key == QStringLiteral("expire"))
            profile.expireAt = number;
    }
}

void ProfileStore::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged(busy);
}

void ProfileStore::importUrl(const QString& name, const QUrl& url)
{
    if (m_busy) {
        emit operationFailed(QStringLiteral("已有订阅任务正在进行"));
        return;
    }
    if (!url.isValid() || (url.scheme() != QStringLiteral("http") &&
                           url.scheme() != QStringLiteral("https"))) {
        emit operationFailed(QStringLiteral("订阅地址必须是有效的 http/https URL"));
        return;
    }
    beginDownload(PendingKind::Import, QString(), name, url);
}

void ProfileStore::refreshProfile(const QString& id)
{
    if (m_busy) {
        emit operationFailed(QStringLiteral("已有订阅任务正在进行"));
        return;
    }
    const auto profile = find(id);
    if (!profile) {
        emit operationFailed(QStringLiteral("找不到这个订阅"));
        return;
    }
    if (!profile->remote() || profile->url.isEmpty()) {
        emit operationFailed(QStringLiteral("本地配置无法从网络更新"));
        return;
    }
    beginDownload(PendingKind::Refresh, id, profile->name, QUrl(profile->url));
}

void ProfileStore::beginDownload(PendingKind kind, const QString& profileId, const QString& name,
                                 const QUrl& url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setRawHeader("Accept", "application/yaml, text/yaml, text/plain, */*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(45000);

    setBusy(true);
    m_pending.kind = kind;
    m_pending.profileId = profileId;
    m_pending.requestedName = name;
    m_pending.url = url;
    m_pending.reply = m_network.get(request);

    QNetworkReply* reply = m_pending.reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finishDownload(reply); });
}

void ProfileStore::finishDownload(QNetworkReply* reply)
{
    reply->deleteLater();
    if (m_pending.reply != reply)
        return;
    m_pending.reply = nullptr;

    const QByteArray data = reply->readAll();
    const QVariant statusVariant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int status = statusVariant.toInt();

    if (reply->error() != QNetworkReply::NoError) {
        fail(QStringLiteral("下载订阅失败：%1").arg(reply->errorString()));
        return;
    }
    if (status >= 400) {
        fail(QStringLiteral("订阅服务器返回 HTTP %1").arg(status));
        return;
    }

    QString validationError;
    if (!validateContent(data, &validationError)) {
        fail(validationError);
        return;
    }

    const Pending pending = m_pending;
    cq::ProfileMeta profile;
    const bool updating = pending.kind == PendingKind::Refresh;
    if (updating) {
        const auto existing = find(pending.profileId);
        if (!existing) {
            fail(QStringLiteral("订阅已被删除"));
            return;
        }
        profile = *existing;
    } else {
        profile.id = makeId();
        profile.type = QStringLiteral("remote");
        profile.url = pending.url.toString();
        profile.fileName = fileNameForId(profile.id);
    }

    QString desiredName = pending.requestedName;
    const QString title = parseProfileTitle(reply->rawHeader("profile-title"));
    if (desiredName.trimmed().isEmpty())
        desiredName = title;
    if (desiredName.trimmed().isEmpty())
        desiredName = pending.url.host();
    if (!updating || !pending.requestedName.trimmed().isEmpty())
        profile.name = updating ? profile.name : uniqueName(desiredName);

    profile.updatedAt = QDateTime::currentDateTime();
    applyUserInfo(reply->rawHeader("subscription-userinfo"), profile);
    bool ok = false;
    const int interval = reply->rawHeader("profile-update-interval").toInt(&ok);
    if (ok && interval > 0)
        profile.updateIntervalHours = qBound(1, interval, 24 * 30);

    QString writeError;
    if (!writeProfileFile(profile.fileName, data, &writeError)) {
        fail(writeError);
        return;
    }

    if (updating) {
        for (cq::ProfileMeta& item : m_profiles) {
            if (item.id == profile.id) {
                item = profile;
                break;
            }
        }
    } else {
        m_profiles.append(profile);
        m_activeId = profile.id;
    }

    saveMetadata();
    setBusy(false);
    emit changed();
    if (!updating)
        emit activeChanged(m_activeId);
    if (updating)
        emit profileUpdated(profile.id);
    emit operationSucceeded(updating ? QStringLiteral("订阅「%1」已更新").arg(profile.name)
                                     : QStringLiteral("已导入订阅「%1」").arg(profile.name));
}

void ProfileStore::fail(const QString& message)
{
    setBusy(false);
    m_pending.reply = nullptr;
    emit operationFailed(message);
}

void ProfileStore::importFile(const QString& path, const QString& name)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit operationFailed(QStringLiteral("无法读取文件：%1").arg(file.errorString()));
        return;
    }
    QByteArray data = file.readAll();
    QString error;
    if (!validateContent(data, &error)) {
        emit operationFailed(error);
        return;
    }

    cq::ProfileMeta profile;
    profile.id = makeId();
    profile.type = QStringLiteral("local");
    profile.fileName = fileNameForId(profile.id);
    profile.updatedAt = QDateTime::currentDateTime();
    profile.name = uniqueName(name.trimmed().isEmpty() ? QFileInfo(path).completeBaseName() : name);

    if (!writeProfileFile(profile.fileName, data, &error)) {
        emit operationFailed(error);
        return;
    }

    m_profiles.append(profile);
    m_activeId = profile.id;
    saveMetadata();
    emit changed();
    emit activeChanged(m_activeId);
    emit operationSucceeded(QStringLiteral("已导入本地配置「%1」").arg(profile.name));
}

void ProfileStore::importContent(const QString& name, const QByteArray& content,
                                 const QString& type, const QUrl& source)
{
    QString error;
    if (!validateContent(content, &error)) {
        emit operationFailed(error);
        return;
    }

    cq::ProfileMeta profile;
    profile.id = makeId();
    profile.type = (type == QStringLiteral("remote")) ? QStringLiteral("remote")
                                                       : QStringLiteral("local");
    profile.url = source.toString();
    profile.fileName = fileNameForId(profile.id);
    profile.updatedAt = QDateTime::currentDateTime();
    profile.name = uniqueName(name);
    if (!writeProfileFile(profile.fileName, content, &error)) {
        emit operationFailed(error);
        return;
    }
    m_profiles.append(profile);
    m_activeId = profile.id;
    saveMetadata();
    emit changed();
    emit activeChanged(m_activeId);
    emit operationSucceeded(QStringLiteral("已导入配置「%1」").arg(profile.name));
}

void ProfileStore::activate(const QString& id)
{
    if (m_activeId == id)
        return;
    if (!find(id))
        return;
    m_activeId = id;
    saveMetadata();
    emit changed();
    emit activeChanged(id);
}

void ProfileStore::removeProfile(const QString& id)
{
    if (m_busy) {
        emit operationFailed(QStringLiteral("订阅任务进行中，暂时无法删除"));
        return;
    }
    int index = -1;
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles[i].id == id) {
            index = i;
            break;
        }
    }
    if (index < 0)
        return;

    const QString removedName = m_profiles[index].name;
    QFile::remove(configPath(m_profiles[index]));
    m_profiles.removeAt(index);
    const bool activeRemoved = m_activeId == id;
    if (activeRemoved) {
        m_activeId.clear();
        if (!m_profiles.isEmpty())
            m_activeId = m_profiles.first().id;
    }
    if (m_profiles.isEmpty())
        ensureBuiltin();
    ensureActive();
    saveMetadata();
    emit changed();
    if (activeRemoved)
        emit activeChanged(m_activeId);
    emit operationSucceeded(QStringLiteral("已删除配置「%1」").arg(removedName));
}

void ProfileStore::seedDemoProfiles()
{
    // Demo data is intentionally only reachable through --demo. It makes the
    // preview build useful for visual QA without touching a real subscription.
    QFile resource(QStringLiteral(":/config/starter.yaml"));
    if (!resource.open(QIODevice::ReadOnly))
        return;
    const QByteArray data = resource.readAll();

    auto addDemo = [&](const QString& name, const QString& type, const QString& url,
                       qint64 used, qint64 total, int daysLeft) {
        for (const auto& existing : m_profiles) {
            if (existing.name == name)
                return;
        }
        cq::ProfileMeta profile;
        profile.id = makeId();
        profile.name = name;
        profile.type = type;
        profile.url = url;
        profile.fileName = fileNameForId(profile.id);
        profile.updatedAt = QDateTime::currentDateTime().addSecs(-3600 * 5);
        profile.upload = used / 6;
        profile.download = used - used / 6;
        profile.total = total;
        profile.expireAt = QDateTime::currentDateTime().addDays(daysLeft).toSecsSinceEpoch();
        profile.updateIntervalHours = 24;
        if (writeProfileFile(profile.fileName, data, nullptr))
            m_profiles.append(profile);
    };

    addDemo(QStringLiteral("Aurora · 流媒体"), QStringLiteral("remote"),
            QStringLiteral("https://example.com/aurora"), 32LL * 1024 * 1024 * 1024,
            100LL * 1024 * 1024 * 1024, 63);
    addDemo(QStringLiteral("Nebula · 游戏专线"), QStringLiteral("remote"),
            QStringLiteral("https://example.com/nebula"), 184LL * 1024 * 1024 * 1024,
            500LL * 1024 * 1024 * 1024, 21);

    for (const cq::ProfileMeta& profile : std::as_const(m_profiles)) {
        if (profile.name == QStringLiteral("Aurora · 流媒体")) {
            m_activeId = profile.id;
            break;
        }
    }
    saveMetadata();
    emit changed();
    emit activeChanged(m_activeId);
}
