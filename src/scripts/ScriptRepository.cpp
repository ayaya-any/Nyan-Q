#include "scripts/ScriptRepository.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimeZone>
#include <QUuid>

#include <algorithm>

namespace nq::scripts {
namespace {

constexpr int kCatalogVersion = 2;
constexpr int kMaxVersions = 10;
constexpr qint64 kMaxScriptSize = 512LL * 1024;

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QDateTime parseTime(const QJsonValue& value)
{
    if (!value.isString())
        return {};
    const QString text = value.toString();
    QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!parsed.isValid())
        parsed = QDateTime::fromString(text, Qt::ISODate);
    if (parsed.isValid())
        parsed.setTimeZone(QTimeZone::UTC);
    return parsed;
}

QString normalizeHash(const QString& value)
{
    const QString trimmed = value.trimmed().toLower();
    static const QRegularExpression hex(QStringLiteral("^[0-9a-f]{64}$"));
    return hex.match(trimmed).hasMatch() ? trimmed : QString();
}

QJsonObject versionToJson(const ScriptVersion& version)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), version.id);
    object.insert(QStringLiteral("sha256"), version.sha256);
    object.insert(QStringLiteral("size"), static_cast<double>(version.size));
    object.insert(QStringLiteral("savedAt"), version.savedAt.toUTC().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("sourceUrl"), version.sourceUrl);
    object.insert(QStringLiteral("note"), version.note);
    object.insert(QStringLiteral("current"), version.current);
    return object;
}

ScriptVersion versionFromJson(const QJsonObject& object)
{
    ScriptVersion version;
    version.id = object.value(QStringLiteral("id")).toString();
    version.sha256 = normalizeHash(object.value(QStringLiteral("sha256")).toString());
    version.size = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble());
    version.savedAt = parseTime(object.value(QStringLiteral("savedAt")));
    version.sourceUrl = object.value(QStringLiteral("sourceUrl")).toString();
    version.note = object.value(QStringLiteral("note")).toString();
    version.current = object.value(QStringLiteral("current")).toBool();
    return version;
}

QJsonObject scriptToJson(const ScriptMeta& script)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), script.id);
    object.insert(QStringLiteral("name"), script.name);
    object.insert(QStringLiteral("description"), script.description);
    object.insert(QStringLiteral("kind"),
                  script.kind == ScriptSourceKind::Remote ? QStringLiteral("remote")
                                                          : QStringLiteral("local"));
    object.insert(QStringLiteral("sourcePath"), script.sourcePath);
    object.insert(QStringLiteral("url"), script.url);
    object.insert(QStringLiteral("fileName"), script.fileName);
    object.insert(QStringLiteral("enabled"), script.enabled);
    object.insert(QStringLiteral("order"), script.order);
    object.insert(QStringLiteral("autoUpdate"), script.autoUpdate);
    object.insert(QStringLiteral("updateIntervalHours"), script.updateIntervalHours);
    object.insert(QStringLiteral("etag"), script.etag);
    object.insert(QStringLiteral("lastModified"), script.lastModified);
    object.insert(QStringLiteral("clientSha256"), script.clientSha256);
    object.insert(QStringLiteral("expectedSha256"), script.expectedSha256);
    object.insert(QStringLiteral("trusted"), script.trusted);
    object.insert(QStringLiteral("lastError"), script.lastError);
    object.insert(QStringLiteral("createdAt"),
                  script.createdAt.toUTC().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("updatedAt"),
                  script.updatedAt.toUTC().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("lastCheckedAt"),
                  script.lastCheckedAt.toUTC().toString(Qt::ISODateWithMs));

    QJsonArray versions;
    for (const ScriptVersion& version : script.versions)
        versions.append(versionToJson(version));
    object.insert(QStringLiteral("versions"), versions);
    return object;
}

ScriptMeta scriptFromJson(const QJsonObject& object)
{
    ScriptMeta script;
    script.id = object.value(QStringLiteral("id")).toString().trimmed();
    script.name = object.value(QStringLiteral("name")).toString().trimmed();
    if (script.name.isEmpty())
        script.name = QStringLiteral("未命名脚本");
    script.description = object.value(QStringLiteral("description")).toString();
    script.kind = object.value(QStringLiteral("kind")).toString() == QStringLiteral("remote")
                      ? ScriptSourceKind::Remote
                      : ScriptSourceKind::Local;
    script.sourcePath = object.value(QStringLiteral("sourcePath")).toString();
    script.url = object.value(QStringLiteral("url")).toString();
    script.fileName = object.value(QStringLiteral("fileName")).toString();
    script.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    script.order = object.value(QStringLiteral("order")).toInt();
    script.autoUpdate = object.value(QStringLiteral("autoUpdate")).toBool(false);
    script.updateIntervalHours =
        qBound(1, object.value(QStringLiteral("updateIntervalHours")).toInt(24), 24 * 30);
    script.etag = object.value(QStringLiteral("etag")).toString();
    script.lastModified = object.value(QStringLiteral("lastModified")).toString();
    script.clientSha256 = normalizeHash(object.value(QStringLiteral("clientSha256")).toString());
    script.expectedSha256 =
        normalizeHash(object.value(QStringLiteral("expectedSha256")).toString());
    script.trusted = object.value(QStringLiteral("trusted")).toBool(false);
    script.lastError = object.value(QStringLiteral("lastError")).toString();
    script.createdAt = parseTime(object.value(QStringLiteral("createdAt")));
    script.updatedAt = parseTime(object.value(QStringLiteral("updatedAt")));
    script.lastCheckedAt = parseTime(object.value(QStringLiteral("lastCheckedAt")));

    const QJsonArray versions = object.value(QStringLiteral("versions")).toArray();
    for (const QJsonValue& value : versions) {
        if (!value.isObject())
            continue;
        ScriptVersion version = versionFromJson(value.toObject());
        if (version.id.isEmpty() || version.sha256.isEmpty())
            continue;
        script.versions.append(version);
    }
    return script;
}

QJsonObject settingsToJson(const ScriptSettings& settings)
{
    QJsonObject object;
    object.insert(QStringLiteral("timeoutMs"), settings.timeoutMs);
    object.insert(QStringLiteral("memoryLimitMb"),
                  static_cast<int>(settings.memoryLimitBytes / (1024 * 1024)));
    object.insert(QStringLiteral("stopOnError"), settings.stopOnError);
    object.insert(QStringLiteral("autoUpdateEnabled"), settings.autoUpdateEnabled);
    object.insert(QStringLiteral("logRetention"), settings.logRetention);
    return object;
}

ScriptSettings settingsFromJson(const QJsonObject& object, ScriptSettings fallback)
{
    ScriptSettings settings = fallback;
    settings.timeoutMs =
        qBound(100, object.value(QStringLiteral("timeoutMs")).toInt(settings.timeoutMs), 120000);
    const int memoryMb = qBound(8, object.value(QStringLiteral("memoryLimitMb")).toInt(
                                       static_cast<int>(settings.memoryLimitBytes / (1024 * 1024))),
                                2048);
    settings.memoryLimitBytes = static_cast<qint64>(memoryMb) * 1024 * 1024;
    settings.stopOnError = object.value(QStringLiteral("stopOnError")).toBool(true);
    settings.autoUpdateEnabled =
        object.value(QStringLiteral("autoUpdateEnabled")).toBool(false);
    settings.logRetention =
        qBound(100, object.value(QStringLiteral("logRetention")).toInt(1000), 10000);
    return settings;
}

QString uniqueVersionId(const QDir& versionDir, const QString& base)
{
    QString candidate = base;
    int suffix = 1;
    while (QFileInfo::exists(versionDir.filePath(candidate + QStringLiteral(".js")))) {
        ++suffix;
        candidate = base + QStringLiteral("-%1").arg(suffix);
    }
    return candidate;
}

}  // namespace

ScriptRepository::ScriptRepository(QString rootDirectory)
    : m_root(QDir::cleanPath(std::move(rootDirectory)))
{
}

bool ScriptRepository::initialize(QString* error)
{
    if (error)
        error->clear();
    QDir dir(m_root);
    if (!QDir().mkpath(m_root)) {
        if (error)
            *error = QStringLiteral("无法创建脚本目录：%1").arg(QDir::toNativeSeparators(m_root));
        return false;
    }
    QDir().mkpath(dir.filePath(QStringLiteral("files")));
    QDir().mkpath(dir.filePath(QStringLiteral("versions")));
    QDir().mkpath(dir.filePath(QStringLiteral("tmp")));
    return load(error);
}

bool ScriptRepository::reload(QString* error)
{
    return load(error);
}

QString ScriptRepository::metadataPath() const
{
    return QDir(m_root).filePath(QStringLiteral("scripts.json"));
}

QString ScriptRepository::contentPath(const ScriptMeta& script) const
{
    return QDir(m_root).filePath(QStringLiteral("files/%1").arg(script.fileName));
}

QString ScriptRepository::versionPath(const ScriptMeta& script, const ScriptVersion& version) const
{
    return versionFileFor(script.id, version.id);
}

QString ScriptRepository::versionFileFor(const QString& scriptId, const QString& versionId) const
{
    return QDir(m_root).filePath(
        QStringLiteral("versions/%1/%2.js").arg(scriptId, versionId));
}

bool ScriptRepository::validId(const QString& value)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$"));
    return pattern.match(value).hasMatch();
}

bool ScriptRepository::validVersionId(const QString& value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,79}$"));
    return pattern.match(value).hasMatch() && !value.contains(QStringLiteral(".."));
}

bool ScriptRepository::isSafeRelative(const QString& value)
{
    if (value.isEmpty() || value.contains(QLatin1Char('\\')) ||
        value.contains(QStringLiteral("..")) || QDir::isAbsolutePath(value))
        return false;
    return true;
}

int ScriptRepository::maxVersions()
{
    return kMaxVersions;
}

qint64 ScriptRepository::maxScriptBytes()
{
    return kMaxScriptSize;
}

bool ScriptRepository::load(QString* error)
{
    m_catalog = Catalog{};
    m_loaded = false;

    QFile file(metadataPath());
    if (!file.exists()) {
        m_loaded = true;
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取脚本元数据：%1").arg(file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("脚本元数据损坏：%1")
                         .arg(parseError.errorString());
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() > kCatalogVersion) {
        if (error)
            *error = QStringLiteral("脚本元数据来自更新版本的 Nyan-Q");
        return false;
    }

    ScriptSettings defaults;
    m_catalog.settings = settingsFromJson(root.value(QStringLiteral("settings")).toObject(),
                                          defaults);

    bool dirty = false;
    const QJsonArray scripts = root.value(QStringLiteral("scripts")).toArray();
    for (const QJsonValue& value : scripts) {
        if (!value.isObject())
            continue;
        ScriptMeta script = scriptFromJson(value.toObject());
        if (!validId(script.id))
            continue;
        bool duplicate = false;
        for (const ScriptMeta& existing : std::as_const(m_catalog.scripts)) {
            if (existing.id == script.id) {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;

        // The repository owns the on-disk layout; never trust a stored path.
        script.fileName = QStringLiteral("script-%1.js").arg(script.id);
        if (!script.createdAt.isValid())
            script.createdAt = QDateTime::currentDateTimeUtc();
        if (!script.updatedAt.isValid())
            script.updatedAt = script.createdAt;
        if (script.expectedSha256.isEmpty())
            script.expectedSha256.clear();

        QFile contentFile(contentPath(script));
        if (contentFile.exists() && contentFile.open(QIODevice::ReadOnly)) {
            const QByteArray data = contentFile.readAll();
            const QString actualHash = QString::fromLatin1(sha256Hex(data));
            if (!script.clientSha256.isEmpty() && script.clientSha256 != actualHash) {
                script.clientSha256 = actualHash;
                dirty = true;
            } else if (script.clientSha256.isEmpty()) {
                script.clientSha256 = actualHash;
                dirty = true;
            }
            if (script.lastError == QStringLiteral("脚本文件缺失"))
                script.lastError.clear();
        } else {
            script.lastError = QStringLiteral("脚本文件缺失");
            dirty = true;
        }

        QVector<ScriptVersion> validVersions;
        for (ScriptVersion version : script.versions) {
            if (!validVersionId(version.id))
                continue;
            if (!QFileInfo::exists(versionFileFor(script.id, version.id)))
                continue;
            validVersions.append(version);
        }
        if (validVersions.size() != script.versions.size()) {
            script.versions = validVersions;
            dirty = true;
        }
        bool currentFound = false;
        for (ScriptVersion& version : script.versions) {
            if (version.current && !currentFound) {
                currentFound = true;
            } else {
                version.current = false;
            }
        }
        if (!currentFound && !script.versions.isEmpty()) {
            for (ScriptVersion& version : script.versions) {
                if (version.sha256 == script.clientSha256) {
                    version.current = true;
                    currentFound = true;
                    break;
                }
            }
            if (!currentFound)
                script.versions.first().current = true;
            dirty = true;
        }
        if (script.versions.size() > kMaxVersions) {
            const QVector<ScriptVersion> expired = script.versions.mid(kMaxVersions);
            script.versions.resize(kMaxVersions);
            for (const ScriptVersion& version : expired)
                QFile::remove(versionFileFor(script.id, version.id));
            dirty = true;
        }

        m_catalog.scripts.append(std::move(script));
    }

    const QJsonArray bindings = root.value(QStringLiteral("bindings")).toArray();
    for (const QJsonValue& value : bindings) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        ScriptBinding binding;
        binding.scriptId = object.value(QStringLiteral("scriptId")).toString();
        binding.profileId = object.value(QStringLiteral("profileId")).toString();
        binding.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        if (binding.scriptId.isEmpty() || binding.profileId.isEmpty() ||
            binding.profileId.size() > 128)
            continue;
        if (!find(binding.scriptId))
            continue;
        bool replaced = false;
        for (ScriptBinding& existing : m_catalog.bindings) {
            if (existing.scriptId == binding.scriptId &&
                existing.profileId == binding.profileId) {
                existing = binding;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            m_catalog.bindings.append(binding);
    }

    m_loaded = true;
    if (dirty) {
        QString saveError;
        if (!save(&saveError)) {
            // The catalog is still usable in memory; a read-only or locked
            // directory must not make the existing scripts disappear.
            if (error)
                *error = saveError;
        }
    }
    return true;
}

bool ScriptRepository::writeCatalog(const Catalog& catalog, QString* error) const
{
    QJsonArray scripts;
    for (const ScriptMeta& script : catalog.scripts)
        scripts.append(scriptToJson(script));

    QJsonArray bindings;
    for (const ScriptBinding& binding : catalog.bindings) {
        QJsonObject object;
        object.insert(QStringLiteral("scriptId"), binding.scriptId);
        object.insert(QStringLiteral("profileId"), binding.profileId);
        object.insert(QStringLiteral("enabled"), binding.enabled);
        bindings.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kCatalogVersion);
    root.insert(QStringLiteral("settings"), settingsToJson(catalog.settings));
    root.insert(QStringLiteral("scripts"), scripts);
    root.insert(QStringLiteral("bindings"), bindings);

    return writeFileAtomic(metadataPath(),
                           QJsonDocument(root).toJson(QJsonDocument::Indented), error);
}

bool ScriptRepository::save(QString* error)
{
    if (!m_loaded && !QFileInfo::exists(metadataPath())) {
        if (error)
            *error = QStringLiteral("脚本存储尚未初始化");
        return false;
    }
    return writeCatalog(m_catalog, error);
}

ScriptMeta* ScriptRepository::find(const QString& id)
{
    for (ScriptMeta& script : m_catalog.scripts) {
        if (script.id == id)
            return &script;
    }
    return nullptr;
}

const ScriptMeta* ScriptRepository::find(const QString& id) const
{
    for (const ScriptMeta& script : m_catalog.scripts) {
        if (script.id == id)
            return &script;
    }
    return nullptr;
}

ScriptVersion ScriptRepository::createVersion(const ScriptMeta& script, const QByteArray& content,
                                              const QString& note) const
{
    ScriptVersion version;
    version.sha256 = QString::fromLatin1(sha256Hex(content));
    version.size = content.size();
    version.savedAt = QDateTime::currentDateTimeUtc();
    version.sourceUrl = script.url;
    version.note = note;
    version.current = true;

    const QDir versionDir(QDir(m_root).filePath(QStringLiteral("versions/%1").arg(script.id)));
    QDir().mkpath(versionDir.absolutePath());
    const QString base =
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")) +
        QLatin1Char('-') + version.sha256.left(12);
    version.id = uniqueVersionId(versionDir, base);
    return version;
}

void ScriptRepository::trimVersions(ScriptMeta* script)
{
    if (!script)
        return;
    if (script->versions.size() <= kMaxVersions)
        return;
    const QVector<ScriptVersion> removed =
        script->versions.mid(kMaxVersions);
    script->versions.resize(kMaxVersions);
    for (const ScriptVersion& version : removed)
        QFile::remove(versionFileFor(script->id, version.id));
}

bool ScriptRepository::addScript(ScriptMeta meta, const QByteArray& content, QString* error)
{
    if (error)
        error->clear();
    if (content.isEmpty() || content.size() > kMaxScriptSize) {
        if (error)
            *error = QStringLiteral("脚本内容为空或超过 %1 KB").arg(kMaxScriptSize / 1024);
        return false;
    }
    if (meta.id.isEmpty())
        meta.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!validId(meta.id)) {
        if (error)
            *error = QStringLiteral("脚本 ID 无效");
        return false;
    }
    if (find(meta.id)) {
        if (error)
            *error = QStringLiteral("脚本 ID 已存在");
        return false;
    }
    meta.name = meta.name.trimmed();
    if (meta.name.isEmpty())
        meta.name = QStringLiteral("未命名脚本");
    meta.fileName = QStringLiteral("script-%1.js").arg(meta.id);
    if (!meta.createdAt.isValid())
        meta.createdAt = QDateTime::currentDateTimeUtc();
    meta.updatedAt = QDateTime::currentDateTimeUtc();
    meta.clientSha256 = QString::fromLatin1(sha256Hex(content));
    meta.expectedSha256 = normalizeHash(meta.expectedSha256);
    meta.updateIntervalHours = qBound(1, meta.updateIntervalHours, 24 * 30);
    meta.lastError.clear();
    meta.versions.clear();

    ScriptVersion version = createVersion(meta, content, QStringLiteral("初始版本"));
    version.current = true;
    meta.versions.append(version);

    QDir(m_root).mkpath(QStringLiteral("files"));
    QString writeError;
    if (!writeFileAtomic(versionFileFor(meta.id, version.id), content, &writeError) ||
        !writeFileAtomic(QDir(m_root).filePath(QStringLiteral("files/%1").arg(meta.fileName)),
                         content, &writeError)) {
        QFile::remove(versionFileFor(meta.id, version.id));
        if (error)
            *error = writeError;
        return false;
    }

    m_catalog.scripts.append(meta);
    if (!save(&writeError)) {
        m_catalog.scripts.removeLast();
        QFile::remove(versionFileFor(meta.id, version.id));
        QFile::remove(contentPath(meta));
        if (error)
            *error = writeError;
        return false;
    }
    m_loaded = true;
    return true;
}

bool ScriptRepository::updateContent(const QString& id, const QByteArray& content,
                                     const QString& note, bool* changed, QString* error)
{
    if (changed)
        *changed = false;
    if (error)
        error->clear();

    if (content.isEmpty() || content.size() > kMaxScriptSize) {
        if (error)
            *error = QStringLiteral("脚本内容为空或超过 %1 KB").arg(kMaxScriptSize / 1024);
        return false;
    }
    ScriptMeta* script = find(id);
    if (!script) {
        if (error)
            *error = QStringLiteral("找不到脚本");
        return false;
    }

    const QString newHash = QString::fromLatin1(sha256Hex(content));
    if (newHash == script->clientSha256) {
        if (error)
            error->clear();
        return true;
    }

    const QByteArray previous = QFileInfo::exists(contentPath(*script))
                                    ? [&] {
                                          QFile file(contentPath(*script));
                                          return file.open(QIODevice::ReadOnly) ? file.readAll()
                                                                                : QByteArray();
                                      }()
                                    : QByteArray();
    const QVector<ScriptVersion> previousVersions = script->versions;

    ScriptVersion version = createVersion(*script, content, note);
    version.current = true;
    for (ScriptVersion& existing : script->versions)
        existing.current = false;
    script->versions.prepend(version);
    QVector<ScriptVersion> expiredVersions;
    if (script->versions.size() > kMaxVersions) {
        expiredVersions = script->versions.mid(kMaxVersions);
        script->versions.resize(kMaxVersions);
    }

    const QString versionFile = versionFileFor(script->id, version.id);
    QString writeError;
    if (!writeFileAtomic(versionFile, content, &writeError) ||
        !writeFileAtomic(contentPath(*script), content, &writeError)) {
        QFile::remove(versionFile);
        script->versions = previousVersions;
        if (error)
            *error = writeError;
        return false;
    }

    script->clientSha256 = newHash;
    script->updatedAt = QDateTime::currentDateTimeUtc();
    script->lastError.clear();

    if (!save(&writeError)) {
        script->versions = previousVersions;
        script->clientSha256 = previous.isEmpty()
                                   ? QString()
                                   : QString::fromLatin1(sha256Hex(previous));
        script->updatedAt = QDateTime::currentDateTimeUtc();
        if (previous.isEmpty())
            QFile::remove(contentPath(*script));
        else
            writeFileAtomic(contentPath(*script), previous, nullptr);
        QFile::remove(versionFile);
        if (error)
            *error = writeError;
        return false;
    }

    // Old versions beyond the retention limit are deleted only after the
    // catalog commit succeeded, so a failed save never loses history.
    for (const ScriptVersion& expired : std::as_const(expiredVersions))
        QFile::remove(versionFileFor(script->id, expired.id));
    if (changed)
        *changed = true;
    return true;
}

bool ScriptRepository::removeScript(const QString& id, QString* error)
{
    if (error)
        error->clear();
    int index = -1;
    for (int i = 0; i < m_catalog.scripts.size(); ++i) {
        if (m_catalog.scripts.at(i).id == id) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        if (error)
            *error = QStringLiteral("找不到脚本");
        return false;
    }

    Catalog backup = m_catalog;
    const ScriptMeta removed = m_catalog.scripts.takeAt(index);
    m_catalog.bindings.erase(
        std::remove_if(m_catalog.bindings.begin(), m_catalog.bindings.end(),
                       [&id](const ScriptBinding& binding) { return binding.scriptId == id; }),
        m_catalog.bindings.end());

    QString saveError;
    if (!save(&saveError)) {
        m_catalog = std::move(backup);
        if (error)
            *error = saveError;
        return false;
    }

    QFile::remove(contentPath(removed));
    QDir versionDir(QDir(m_root).filePath(QStringLiteral("versions/%1").arg(removed.id)));
    if (versionDir.exists() && validId(removed.id))
        versionDir.removeRecursively();
    return true;
}

QByteArray ScriptRepository::content(const QString& id, QString* error) const
{
    if (error)
        error->clear();
    const ScriptMeta* script = find(id);
    if (!script) {
        if (error)
            *error = QStringLiteral("找不到脚本");
        return {};
    }
    QFile file(contentPath(*script));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取脚本缓存：%1").arg(file.errorString());
        return {};
    }
    const QByteArray data = file.readAll();
    if (data.size() > kMaxScriptSize) {
        if (error)
            *error = QStringLiteral("脚本缓存超过 %1 KB").arg(kMaxScriptSize / 1024);
        return {};
    }
    return data;
}

QByteArray ScriptRepository::versionContent(const QString& id, const QString& versionId,
                                            QString* error) const
{
    if (error)
        error->clear();
    const ScriptMeta* script = find(id);
    if (!script || !validVersionId(versionId)) {
        if (error)
            *error = QStringLiteral("找不到脚本版本");
        return {};
    }
    QFile file(versionFileFor(id, versionId));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取历史版本：%1").arg(file.errorString());
        return {};
    }
    return file.readAll();
}

bool ScriptRepository::restoreVersion(const QString& id, const QString& versionId, QString* error)
{
    if (error)
        error->clear();
    const QByteArray data = versionContent(id, versionId, error);
    if (data.isEmpty())
        return false;
    const ScriptMeta* script = find(id);
    if (!script)
        return false;
    const ScriptVersion* version = nullptr;
    for (const ScriptVersion& candidate : script->versions) {
        if (candidate.id == versionId) {
            version = &candidate;
            break;
        }
    }
    if (!version) {
        if (error)
            *error = QStringLiteral("找不到脚本版本");
        return false;
    }
    return updateContent(id, data,
                         QStringLiteral("回滚到版本 %1").arg(versionId.left(20)), nullptr, error);
}

}  // namespace nq::scripts
