#include "scripts/ScriptManager.h"

#include "scripts/ScriptEngine.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include <algorithm>

namespace nq::scripts {
namespace {

constexpr qint64 kMaxScriptBytes = 512LL * 1024;
constexpr int kAutoCheckIntervalMs = 15 * 60 * 1000;

QString defaultScriptTemplate()
{
    return QStringLiteral(
        "// Nyan-Q JavaScript 配置覆写\n"
        "// config: 当前 Mihomo 配置对象；profileName: 订阅名称\n"
        "// 可用 console.log / console.warn / console.error 输出日志\n\n"
        "function main(config, profileName) {\n"
        "    // 在这里修改 config，例如：\n"
        "    // config[\"rules\"].unshift(\"DOMAIN-SUFFIX,example.com,Proxy\");\n"
        "    return config;\n"
        "}\n");
}

bool validateSource(const QByteArray& data, const QString& name, QString* error)
{
    if (error)
        error->clear();
    if (data.isEmpty()) {
        if (error)
            *error = QStringLiteral("脚本内容为空");
        return false;
    }
    if (data.size() > kMaxScriptBytes) {
        if (error)
            *error = QStringLiteral("脚本内容超过 %1 KB 限制").arg(kMaxScriptBytes / 1024);
        return false;
    }
    const QString text = QString::fromUtf8(data);
    if (text.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("脚本内容为空");
        return false;
    }
    QString engineError;
    int line = 0;
    int column = 0;
    if (!ScriptEngine::validate(text, name, &engineError, &line, &column)) {
        if (error) {
            *error = QStringLiteral("JavaScript 语法错误：%1").arg(engineError);
            if (line > 0)
                *error += QStringLiteral("（第 %1 行，第 %2 列）").arg(line).arg(column);
        }
        return false;
    }
    return true;
}

QString normalizedHash(const QString& value)
{
    const QString trimmed = value.trimmed().toLower();
    static const QRegularExpression hex(QStringLiteral("^[0-9a-f]{64}$"));
    return hex.match(trimmed).hasMatch() ? trimmed : QString();
}

QString currentVersionId(const ScriptMeta& script)
{
    for (const ScriptVersion& version : script.versions) {
        if (version.current)
            return version.id;
    }
    return script.versions.isEmpty() ? QString() : script.versions.first().id;
}

}  // namespace

ScriptManager::ScriptManager(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<ScriptUpdateResult>("nq::scripts::ScriptUpdateResult");
    qRegisterMetaType<ScriptLogEntry>("nq::scripts::ScriptLogEntry");
    connect(&m_downloader, &ScriptDownloader::finished, this,
            &ScriptManager::handleDownloadFinished);
    m_autoTimer.setInterval(kAutoCheckIntervalMs);
    connect(&m_autoTimer, &QTimer::timeout, this, &ScriptManager::maybeAutoCheck);
}

ScriptManager::~ScriptManager()
{
    shutdown();
}

bool ScriptManager::initialize(const QString& scriptsDirectory, QString* error)
{
    if (error)
        error->clear();
    if (m_initialized)
        return true;

    m_repository = ScriptRepository(scriptsDirectory);
    if (!m_repository.initialize(error))
        return false;

    m_bindings.setBindings(m_repository.catalog().bindings);
    m_logs.setFilePath(QDir(scriptsDirectory).filePath(QStringLiteral("script.log")));
    m_logs.setCapacity(m_repository.settings().logRetention);
    m_logs.loadExisting();
    m_autoTimer.start();
    m_initialized = true;
    return true;
}

void ScriptManager::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    m_autoTimer.stop();
    m_downloader.abortAll();
    m_downloads.clear();
    m_updateInFlight.clear();
    if (m_busy) {
        m_busy = false;
        emit busyChanged(false);
    }
    m_initialized = false;
}

const QVector<ScriptMeta>& ScriptManager::scripts() const
{
    return m_repository.catalog().scripts;
}

std::optional<ScriptMeta> ScriptManager::find(const QString& id) const
{
    if (const ScriptMeta* script = m_repository.find(id))
        return *script;
    return std::nullopt;
}

QByteArray ScriptManager::content(const QString& id, QString* error) const
{
    return m_repository.content(id, error);
}

QByteArray ScriptManager::versionContent(const QString& id, const QString& versionId,
                                         QString* error) const
{
    return m_repository.versionContent(id, versionId, error);
}

bool ScriptManager::saveCatalog(QString* error)
{
    if (!m_repository.save(error))
        return false;
    emit changed();
    return true;
}

bool ScriptManager::syncBindings(QString* error)
{
    m_repository.catalog().bindings = m_bindings.bindings();
    return saveCatalog(error);
}

QString ScriptManager::uniqueName(const QString& desired) const
{
    QString base = desired.trimmed();
    if (base.isEmpty())
        base = QStringLiteral("未命名脚本");
    if (base.size() > 80)
        base = base.left(80);
    QString candidate = base;
    int suffix = 2;
    while (true) {
        bool clash = false;
        for (const ScriptMeta& script : m_repository.catalog().scripts) {
            if (script.name.compare(candidate, Qt::CaseInsensitive) == 0) {
                clash = true;
                break;
            }
        }
        if (!clash)
            return candidate;
        candidate = QStringLiteral("%1 (%2)").arg(base).arg(suffix++);
    }
}

QString ScriptManager::addScriptWithBinding(ScriptMeta meta, const QByteArray& contentData,
                                            QString* error)
{
    if (meta.id.isEmpty())
        meta.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (meta.order <= 0) {
        int maximum = -10;
        for (const ScriptMeta& script : m_repository.catalog().scripts)
            maximum = qMax(maximum, script.order);
        meta.order = maximum + 10;
    }
    meta.name = uniqueName(meta.name);
    if (!m_repository.addScript(meta, contentData, error))
        return {};

    m_bindings.setGlobal(meta.id, true);
    QString bindError;
    if (!syncBindings(&bindError)) {
        m_bindings.removeScript(meta.id);
        m_repository.removeScript(meta.id, nullptr);
        if (error)
            *error = bindError;
        return {};
    }
    emit changed();
    return meta.id;
}

QString ScriptManager::createLocalScript(const QString& name, const QString& description,
                                         const QString& source, QString* error)
{
    if (error)
        error->clear();
    if (!m_initialized) {
        if (error)
            *error = QStringLiteral("脚本系统尚未初始化");
        return {};
    }
    const QString contentText = source.trimmed().isEmpty() ? defaultScriptTemplate() : source;
    const QByteArray data = contentText.toUtf8();
    if (!validateSource(data, name, error))
        return {};

    ScriptMeta meta;
    meta.name = name;
    meta.description = description;
    meta.kind = ScriptSourceKind::Local;
    meta.trusted = true;
    const QString id = addScriptWithBinding(meta, data, error);
    if (!id.isEmpty())
        emit operationSucceeded(QStringLiteral("已创建脚本「%1」").arg(meta.name));
    return id;
}

QString ScriptManager::importLocalFile(const QString& path, const QString& name, QString* error)
{
    if (error)
        error->clear();
    if (!m_initialized) {
        if (error)
            *error = QStringLiteral("脚本系统尚未初始化");
        return {};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取脚本文件：%1").arg(file.errorString());
        return {};
    }
    const QByteArray data = file.readAll();
    const QString scriptName =
        name.trimmed().isEmpty() ? QFileInfo(path).completeBaseName() : name;
    if (!validateSource(data, scriptName, error))
        return {};

    ScriptMeta meta;
    meta.name = scriptName;
    meta.description = QStringLiteral("从本地文件导入");
    meta.kind = ScriptSourceKind::Local;
    meta.sourcePath = QFileInfo(path).absoluteFilePath();
    meta.trusted = true;
    const QString id = addScriptWithBinding(meta, data, error);
    if (!id.isEmpty())
        emit operationSucceeded(QStringLiteral("已导入本地脚本「%1」").arg(meta.name));
    return id;
}

bool ScriptManager::importRemote(const QString& name, const QUrl& url, bool trusted,
                                 QString* error)
{
    if (error)
        error->clear();
    if (!m_initialized) {
        if (error)
            *error = QStringLiteral("脚本系统尚未初始化");
        return false;
    }
    if (!trusted) {
        if (error)
            *error = QStringLiteral("导入远程脚本前必须确认来源 URL");
        return false;
    }
    QString urlError;
    if (!ScriptDownloader::isAllowedUrl(url, &urlError)) {
        if (error)
            *error = urlError;
        return false;
    }

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    DownloadContext context;
    context.scriptId = id;
    context.scriptName = uniqueName(name);
    context.import = true;
    context.importName = context.scriptName;
    context.importUrl = url;
    context.manual = true;
    m_downloads.insert(id, context);
    m_updateInFlight.insert(id);
    setBusy();
    emit updateStarted(id, context.scriptName);

    ScriptDownloadRequest request;
    request.requestId = id;
    request.scriptId = id;
    request.url = url;
    request.manual = true;
    request.maxBytes = kMaxScriptBytes + 1;
    request.timeoutMs = 30000;
    m_downloader.download(request);
    return true;
}

bool ScriptManager::saveContent(const QString& id, const QByteArray& sourceData, QString* error)
{
    if (error)
        error->clear();
    ScriptMeta* script = m_repository.find(id);
    if (!script) {
        if (error)
            *error = QStringLiteral("找不到脚本");
        return false;
    }
    if (!validateSource(sourceData, script->name, error))
        return false;

    bool changed = false;
    if (!m_repository.updateContent(id, sourceData,
                                    QStringLiteral("手动保存"),
                                    &changed, error)) {
        return false;
    }
    script = m_repository.find(id);
    if (!script) {
        if (error)
            *error = QStringLiteral("脚本在保存过程中被删除");
        return false;
    }
    script->lastError.clear();
    if (script->remote()) {
        // Local edits diverge from the remote copy; force a conditional
        // download next time instead of assuming the cached validator.
        script->etag.clear();
        script->lastModified.clear();
    }
    saveCatalog(nullptr);
    emit scriptUpdated(id);
    emit operationSucceeded(QStringLiteral("脚本「%1」已保存").arg(script->name));
    return true;
}

bool ScriptManager::renameScript(const QString& id, const QString& name, QString* error)
{
    if (error)
        error->clear();
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 80) {
        if (error)
            *error = QStringLiteral("脚本名称不能为空且不能超过 80 个字符");
        return false;
    }
    ScriptMeta* script = m_repository.find(id);
    if (!script) {
        if (error)
            *error = QStringLiteral("找不到脚本");
        return false;
    }
    for (const ScriptMeta& other : m_repository.catalog().scripts) {
        if (other.id != id && other.name.compare(trimmed, Qt::CaseInsensitive) == 0) {
            if (error)
                *error = QStringLiteral("已存在同名脚本");
            return false;
        }
    }
    script->name = trimmed;
    script->updatedAt = QDateTime::currentDateTimeUtc();
    if (!saveCatalog(error))
        return false;
    emit scriptUpdated(id);
    emit operationSucceeded(QStringLiteral("脚本名称已更新"));
    return true;
}

bool ScriptManager::removeScript(const QString& id, QString* error)
{
    if (error)
        error->clear();
    const ScriptMeta* script = m_repository.find(id);
    if (!script) {
        if (error)
            *error = QStringLiteral("找不到脚本");
        return false;
    }
    const QString name = script->name;
    m_bindings.removeScript(id);
    m_pendingVerification.remove(id);
    m_repository.catalog().bindings = m_bindings.bindings();
    if (!m_repository.removeScript(id, error))
        return false;
    emit changed();
    emit scriptUpdated(id);
    emit operationSucceeded(QStringLiteral("已删除脚本「%1」").arg(name));
    return true;
}

bool ScriptManager::setEnabled(const QString& id, bool enabled, QString* error)
{
    ScriptMeta* script = m_repository.find(id);
    if (!script) {
        if (error)
            *error = QStringLiteral("找不到脚本");
        return false;
    }
    if (script->enabled == enabled)
        return true;
    script->enabled = enabled;
    script->updatedAt = QDateTime::currentDateTimeUtc();
    if (!saveCatalog(error))
        return false;
    emit scriptUpdated(id);
    return true;
}

bool ScriptManager::setOrder(const QString& id, int order, QString* error)
{
    ScriptMeta* script = m_repository.find(id);
    if (!script) {
        if (error)
            *error = QStringLiteral("找不到脚本");
        return false;
    }
    script->order = qMax(0, order);
    if (!saveCatalog(error))
        return false;
    emit scriptUpdated(id);
    return true;
}

bool ScriptManager::moveScript(const QString& id, int delta, QString* error)
{
    if (delta == 0)
        return true;
    QVector<ScriptMeta*> ordered;
    for (ScriptMeta& script : m_repository.catalog().scripts)
        ordered.append(&script);
    std::stable_sort(ordered.begin(), ordered.end(), [](const ScriptMeta* a, const ScriptMeta* b) {
        if (a->order != b->order)
            return a->order < b->order;
        return QString::localeAwareCompare(a->name, b->name) < 0;
    });
    int index = -1;
    for (int i = 0; i < ordered.size(); ++i) {
        if (ordered.at(i)->id == id) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        if (error)
            *error = QStringLiteral("找不到脚本");
        return false;
    }
    const int target = qBound(0, index + delta, ordered.size() - 1);
    if (target == index)
        return true;
    std::swap(ordered[index], ordered[target]);
    for (int i = 0; i < ordered.size(); ++i)
        ordered.at(i)->order = i * 10;
    if (!saveCatalog(error))
        return false;
    emit scriptUpdated(id);
    return true;
}

bool ScriptManager::setAutoUpdate(const QString& id, bool enabled, QString* error)
{
    ScriptMeta* script = m_repository.find(id);
    if (!script) {
        if (error)
            *error = QStringLiteral("找不到脚本");
        return false;
    }
    if (!script->remote() && enabled) {
        if (error)
            *error = QStringLiteral("本地脚本没有自动更新");
        return false;
    }
    script->autoUpdate = enabled;
    if (!saveCatalog(error))
        return false;
    emit changed();
    return true;
}

bool ScriptManager::setUpdateInterval(const QString& id, int hours, QString* error)
{
    ScriptMeta* script = m_repository.find(id);
    if (!script) {
        if (error)
            *error = QStringLiteral("找不到脚本");
        return false;
    }
    script->updateIntervalHours = qBound(1, hours, 24 * 30);
    if (!saveCatalog(error))
        return false;
    emit changed();
    return true;
}

bool ScriptManager::setExpectedSha256(const QString& id, const QString& hash, QString* error)
{
    if (error)
        error->clear();
    ScriptMeta* script = m_repository.find(id);
    if (!script) {
        if (error)
            *error = QStringLiteral("找不到脚本");
        return false;
    }
    const QString trimmed = hash.trimmed();
    if (trimmed.isEmpty()) {
        script->expectedSha256.clear();
    } else {
        const QString normalized = normalizedHash(trimmed);
        if (normalized.isEmpty()) {
            if (error)
                *error = QStringLiteral("预期 SHA-256 必须是 64 位十六进制字符串");
            return false;
        }
        script->expectedSha256 = normalized;
    }
    if (!saveCatalog(error))
        return false;
    emit changed();
    emit operationSucceeded(trimmed.isEmpty() ? QStringLiteral("已清除脚本哈希固定")
                                               : QStringLiteral("已保存脚本哈希固定值"));
    return true;
}

bool ScriptManager::setTrusted(const QString& id, bool trusted, QString* error)
{
    ScriptMeta* script = m_repository.find(id);
    if (!script) {
        if (error)
            *error = QStringLiteral("找不到脚本");
        return false;
    }
    if (!script->remote()) {
        if (error)
            *error = QStringLiteral("本地脚本不需要确认来源");
        return false;
    }
    script->trusted = trusted;
    if (!saveCatalog(error))
        return false;
    emit changed();
    return true;
}

bool ScriptManager::rollback(const QString& id, const QString& versionId, QString* error)
{
    if (error)
        error->clear();
    if (!m_repository.restoreVersion(id, versionId, error))
        return false;
    ScriptMeta* script = m_repository.find(id);
    if (script)
        script->lastError.clear();
    saveCatalog(nullptr);
    m_pendingVerification.remove(id);
    emit scriptUpdated(id);
    emit operationSucceeded(QStringLiteral("脚本「%1」已回滚").arg(script ? script->name : id));
    return true;
}

bool ScriptManager::rollbackToPrevious(const QString& id, QString* error)
{
    const ScriptMeta* script = m_repository.find(id);
    if (!script) {
        if (error)
            *error = QStringLiteral("找不到脚本");
        return false;
    }
    int currentIndex = -1;
    for (int i = 0; i < script->versions.size(); ++i) {
        if (script->versions.at(i).current) {
            currentIndex = i;
            break;
        }
    }
    if (currentIndex < 0 && !script->versions.isEmpty())
        currentIndex = 0;
    const int previousIndex = currentIndex + 1;
    if (currentIndex < 0 || previousIndex >= script->versions.size()) {
        if (error)
            *error = QStringLiteral("没有更早的脚本版本可回滚");
        return false;
    }
    const QString previousId = script->versions.at(previousIndex).id;
    return rollback(id, previousId, error);
}

bool ScriptManager::setGlobalBinding(const QString& id, bool enabled, QString* error)
{
    if (!m_repository.find(id)) {
        if (error)
            *error = QStringLiteral("找不到脚本");
        return false;
    }
    const QVector<ScriptBinding> previous = m_bindings.bindings();
    m_bindings.setGlobal(id, enabled);
    if (!syncBindings(error)) {
        m_bindings.setBindings(previous);
        m_repository.catalog().bindings = previous;
        return false;
    }
    emit scriptUpdated(id);
    return true;
}

bool ScriptManager::setProfileBinding(const QString& id, const QString& profileId,
                                      bool enabled, QString* error)
{
    if (!m_repository.find(id)) {
        if (error)
            *error = QStringLiteral("找不到脚本");
        return false;
    }
    if (profileId.isEmpty() || profileId == QStringLiteral("*")) {
        if (error)
            *error = QStringLiteral("订阅 ID 无效");
        return false;
    }
    const QVector<ScriptBinding> previous = m_bindings.bindings();
    m_bindings.setProfile(id, profileId, enabled);
    if (!syncBindings(error)) {
        m_bindings.setBindings(previous);
        m_repository.catalog().bindings = previous;
        return false;
    }
    emit scriptUpdated(id);
    return true;
}

bool ScriptManager::isScriptUsedByProfile(const QString& id, const QString& profileId) const
{
    return m_bindings.isUsedByProfile(id, profileId);
}

void ScriptManager::setActiveProfileId(const QString& profileId)
{
    if (m_activeProfileId == profileId)
        return;
    m_activeProfileId = profileId;
    // A pending version belongs to the config generation that was in flight
    // for the previous active profile; do not roll it back against a different
    // subscription later.
    m_pendingVerification.clear();
}

void ScriptManager::retainProfiles(const QStringList& profileIds, QString* error)
{
    const QSet<QString> known(profileIds.begin(), profileIds.end());
    QSet<QString> stale;
    for (const ScriptBinding& binding : m_bindings.bindings()) {
        if (binding.profileId != QStringLiteral("*") && !known.contains(binding.profileId))
            stale.insert(binding.profileId);
    }
    if (stale.isEmpty())
        return;
    for (const QString& profileId : stale)
        m_bindings.removeProfile(profileId);
    syncBindings(error);
}

QVector<ScriptStep> ScriptManager::resolveSteps(const QString& profileId) const
{
    QVector<ScriptStep> steps;
    const QVector<ResolvedScript> resolved =
        m_bindings.resolve(m_repository.catalog().scripts, profileId);
    for (const ResolvedScript& item : resolved) {
        ScriptStep step;
        step.scriptId = item.script.id;
        step.name = item.script.name;
        step.scope = item.scope;
        step.order = item.script.order;
        step.versionSha256 = item.script.clientSha256;
        step.sourceUrl = item.script.url;

        QString readError;
        step.source = QString::fromUtf8(m_repository.content(item.script.id, &readError));
        if (!readError.isEmpty())
            step.integrityError = QStringLiteral("无法读取脚本「%1」：%2")
                                      .arg(item.script.name, readError);
        else if (!item.script.expectedSha256.isEmpty() &&
                 item.script.expectedSha256.compare(item.script.clientSha256,
                                                    Qt::CaseInsensitive) != 0) {
            step.integrityError =
                QStringLiteral("脚本「%1」的当前 SHA-256 与用户固定值不匹配，已拒绝执行")
                    .arg(item.script.name);
        }
        steps.append(step);
    }
    return steps;
}

bool ScriptManager::updateSettings(const ScriptSettings& settings, QString* error)
{
    if (!settings.isValid()) {
        if (error)
            *error = QStringLiteral("脚本设置无效（超时、内存或日志上限超出范围）");
        return false;
    }
    m_repository.settings() = settings;
    if (!saveCatalog(error))
        return false;
    m_logs.setCapacity(settings.logRetention);
    emit changed();
    return true;
}

void ScriptManager::setBusy()
{
    const bool busy = !m_updateInFlight.isEmpty();
    if (busy == m_busy)
        return;
    m_busy = busy;
    emit busyChanged(busy);
}

void ScriptManager::markError(const QString& id, const QString& message)
{
    ScriptMeta* script = m_repository.find(id);
    if (!script)
        return;
    script->lastError = message;
    saveCatalog(nullptr);
}

bool ScriptManager::dueForCheck(const ScriptMeta& script) const
{
    if (!script.remote() || !script.autoUpdate)
        return false;
    if (!script.lastCheckedAt.isValid())
        return true;
    const qint64 seconds =
        static_cast<qint64>(script.updateIntervalHours) * 3600;
    return script.lastCheckedAt.toUTC().secsTo(QDateTime::currentDateTimeUtc()) >= seconds;
}

void ScriptManager::maybeAutoCheck()
{
    if (!m_initialized || m_shuttingDown || !m_repository.settings().autoUpdateEnabled)
        return;
    for (const ScriptMeta& script : m_repository.catalog().scripts) {
        if (!script.enabled || m_updateInFlight.contains(script.id) || !dueForCheck(script))
            continue;
        checkForUpdate(script.id, false);
    }
}

void ScriptManager::checkAll(bool manual)
{
    for (const ScriptMeta& script : m_repository.catalog().scripts) {
        if (!script.remote())
            continue;
        if (!manual) {
            if (!script.enabled || !m_repository.settings().autoUpdateEnabled ||
                !script.autoUpdate || !dueForCheck(script))
                continue;
        }
        checkForUpdate(script.id, manual);
    }
}

void ScriptManager::checkUpdatesForProfile(const QString& profileId, bool manual)
{
    for (const ScriptMeta& script : m_repository.catalog().scripts) {
        if (!script.remote() || !m_bindings.isUsedByProfile(script.id, profileId))
            continue;
        if (!manual) {
            if (!script.enabled || !m_repository.settings().autoUpdateEnabled ||
                !script.autoUpdate || !dueForCheck(script))
                continue;
        }
        checkForUpdate(script.id, manual);
    }
}

void ScriptManager::checkForUpdate(const QString& id, bool manual)
{
    if (!m_initialized || m_shuttingDown)
        return;
    ScriptMeta* script = m_repository.find(id);
    if (!script) {
        if (manual)
            emit operationFailed(QStringLiteral("找不到脚本"));
        return;
    }
    if (!script->remote() || script->url.isEmpty()) {
        if (manual)
            emit operationFailed(QStringLiteral("本地脚本无法从网络更新"));
        return;
    }
    if (!manual) {
        if (!m_repository.settings().autoUpdateEnabled || !script->autoUpdate)
            return;
    }
    if (m_updateInFlight.contains(id)) {
        if (manual)
            emit operationFailed(QStringLiteral("脚本「%1」正在更新").arg(script->name));
        return;
    }

    QString urlError;
    const QUrl url(script->url);
    if (!ScriptDownloader::isAllowedUrl(url, &urlError)) {
        markError(id, urlError);
        if (manual)
            emit operationFailed(urlError);
        return;
    }

    DownloadContext context;
    context.scriptId = id;
    context.scriptName = script->name;
    context.manual = manual;
    context.previousContent = m_repository.content(id);
    context.previousVersionId = currentVersionId(*script);
    m_downloads.insert(id, context);
    m_updateInFlight.insert(id);
    setBusy();
    emit updateStarted(id, script->name);

    ScriptDownloadRequest request;
    request.requestId = id + QStringLiteral(":")
                        + QString::number(QDateTime::currentMSecsSinceEpoch());
    request.scriptId = id;
    request.url = url;
    request.etag = script->etag;
    request.lastModified = script->lastModified;
    request.manual = manual;
    request.timeoutMs = 30000;
    request.maxBytes = kMaxScriptBytes + 1;
    m_downloader.download(request);
}

void ScriptManager::finishUpdate(const ScriptUpdateResult& update)
{
    m_logs.recordUpdate(update);
    emitLogTail();
    emit updateFinished(update);
    if (!update.ok) {
        if (update.manual)
            emit operationFailed(QStringLiteral("脚本「%1」更新失败：%2")
                                     .arg(update.scriptName, update.error));
        return;
    }
    if (update.notModified) {
        if (update.manual)
            emit operationSucceeded(QStringLiteral("脚本「%1」已是最新版本").arg(update.scriptName));
        return;
    }
    if (update.skipped) {
        return;
    }
    emit operationSucceeded(QStringLiteral("脚本「%1」已更新").arg(update.scriptName));
}

void ScriptManager::handleDownloadFinished(const ScriptDownloadResult& download)
{
    DownloadContext context = m_downloads.take(download.scriptId);
    if (context.scriptId.isEmpty()) {
        m_updateInFlight.remove(download.scriptId);
        setBusy();
        return;
    }
    m_updateInFlight.remove(download.scriptId);
    setBusy();
    if (m_shuttingDown)
        return;

    ScriptUpdateResult update;
    update.scriptId = download.scriptId;
    update.scriptName = context.scriptName;
    update.manual = context.manual || context.import;
    update.checkedAt = QDateTime::currentDateTimeUtc();
    update.httpError = download.httpError;
    update.httpStatus = download.httpStatus;
    update.bytes = download.bytes;
    update.newContent = download.data;

    ScriptMeta* script = m_repository.find(download.scriptId);

    if (context.import) {
        update.scriptName = context.importName;
        if (!download.ok) {
            update.error = download.error;
            finishUpdate(update);
            return;
        }
        update.newSha256 = QString::fromLatin1(sha256Hex(download.data));
        QString validationError;
        if (!validateSource(download.data, context.importName, &validationError)) {
            update.error = validationError;
            finishUpdate(update);
            return;
        }

        ScriptMeta meta;
        meta.id = download.scriptId;
        meta.name = context.importName;
        meta.kind = ScriptSourceKind::Remote;
        meta.url = context.importUrl.toString();
        meta.trusted = true;
        meta.etag = download.etag;
        meta.lastModified = download.lastModified;
        meta.lastCheckedAt = QDateTime::currentDateTimeUtc();
        meta.updateIntervalHours = 24;
        const QString created =
            addScriptWithBinding(meta, download.data, nullptr);
        if (created.isEmpty()) {
            update.error = QStringLiteral("无法保存远程脚本");
            finishUpdate(update);
            return;
        }
        script = m_repository.find(created);
        if (script) {
            script->etag = download.etag;
            script->lastModified = download.lastModified;
            script->lastCheckedAt = QDateTime::currentDateTimeUtc();
            script->lastError.clear();
            saveCatalog(nullptr);
            update.scriptName = script->name;
            update.oldContent = {};
            update.newContent = download.data;
            update.oldSha256.clear();
            update.newSha256 = script->clientSha256;
        }
        update.ok = true;
        emit scriptUpdated(created);
        finishUpdate(update);
        return;
    }

    if (!script) {
        update.error = QStringLiteral("脚本已被删除");
        finishUpdate(update);
        return;
    }

    if (!download.ok) {
        update.error = download.error;
        script->lastCheckedAt = update.checkedAt;
        if (!download.etag.isEmpty())
            script->etag = download.etag;
        if (!download.lastModified.isEmpty())
            script->lastModified = download.lastModified;
        script->lastError = update.error;
        saveCatalog(nullptr);
        finishUpdate(update);
        return;
    }

    script->lastCheckedAt = update.checkedAt;
    if (!download.etag.isEmpty())
        script->etag = download.etag;
    if (!download.lastModified.isEmpty())
        script->lastModified = download.lastModified;

    if (download.notModified) {
        script->lastError.clear();
        saveCatalog(nullptr);
        update.ok = true;
        update.notModified = true;
        finishUpdate(update);
        return;
    }

    update.oldSha256 = script->clientSha256;
    update.newSha256 = QString::fromLatin1(sha256Hex(download.data));
    update.oldContent = context.previousContent;

    if (!script->expectedSha256.isEmpty() &&
        script->expectedSha256.compare(update.newSha256, Qt::CaseInsensitive) != 0) {
        update.error = QStringLiteral(
            "下载内容的 SHA-256 与用户固定值不匹配，已拒绝本次更新");
        script->lastError = update.error;
        saveCatalog(nullptr);
        finishUpdate(update);
        return;
    }

    if (update.newSha256 == script->clientSha256) {
        script->lastError.clear();
        saveCatalog(nullptr);
        update.ok = true;
        update.skipped = true;
        finishUpdate(update);
        return;
    }

    QString validationError;
    if (!validateSource(download.data, script->name, &validationError)) {
        update.error = validationError;
        script->lastError = validationError;
        saveCatalog(nullptr);
        finishUpdate(update);
        return;
    }

    bool changed = false;
    QString updateError;
    if (!m_repository.updateContent(download.scriptId, download.data,
                                    QStringLiteral("远程更新 %1")
                                        .arg(update.newSha256.left(12)),
                                    &changed, &updateError)) {
        update.error = updateError;
        script->lastError = updateError;
        saveCatalog(nullptr);
        finishUpdate(update);
        return;
    }

    script = m_repository.find(download.scriptId);
    if (script) {
        script->etag = download.etag;
        script->lastModified = download.lastModified;
        script->lastCheckedAt = update.checkedAt;
        script->lastError.clear();
        saveCatalog(nullptr);
    }
    if (!context.previousVersionId.isEmpty() &&
        (m_activeProfileId.isEmpty() ||
         m_bindings.isUsedByProfile(download.scriptId, m_activeProfileId))) {
        m_pendingVerification.insert(download.scriptId, context.previousVersionId);
    }

    update.ok = true;
    update.newContent = download.data;
    emit scriptUpdated(download.scriptId);
    finishUpdate(update);
}

void ScriptManager::clearLogs()
{
    m_logs.clear();
    emit changed();
}

void ScriptManager::recordPipelineResult(const ScriptPipelineResult& result,
                                         const QString& profileName)
{
    for (const ScriptStepResult& step : result.steps) {
        m_logs.recordRun(profileName, step);
        emitLogTail();
        if (result.ok && step.ok)
            m_pendingVerification.remove(step.scriptId);
    }
    if (!result.ok && !result.error.isEmpty() && result.steps.isEmpty()) {
        m_logs.append(ScriptLogLevel::Error, QString(), QString(), result.error);
        emitLogTail();
    }
}

void ScriptManager::emitLogTail()
{
    const QVector<ScriptLogEntry> entries = m_logs.entries();
    if (!entries.isEmpty())
        emit logAdded(entries.last());
}

void ScriptManager::confirmPendingVersions()
{
    m_pendingVerification.clear();
}

void ScriptManager::handlePipelineFailure(const ScriptPipelineResult& result)
{
    if (result.failingScriptId.isEmpty())
        return;
    rollbackPending(result.failingScriptId);
}

void ScriptManager::rollbackPending(const QString& scriptId)
{
    const auto found = m_pendingVerification.constFind(scriptId);
    if (found == m_pendingVerification.constEnd())
        return;
    const QString previousVersion = found.value();
    m_pendingVerification.erase(found);
    if (previousVersion.isEmpty())
        return;

    QString error;
    if (rollback(scriptId, previousVersion, &error)) {
        const auto script = find(scriptId);
        emit operationFailed(QStringLiteral("脚本「%1」的新版本执行失败，已自动回滚")
                                 .arg(script ? script->name : scriptId));
    } else {
        emit operationFailed(QStringLiteral("脚本「%1」自动回滚失败：%2")
                                 .arg(scriptId, error));
    }
}

}  // namespace nq::scripts
