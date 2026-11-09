#include "scripts/ScriptLogManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace nq::scripts {
namespace {
constexpr qint64 kMaxLogFileBytes = 1024LL * 1024;
}

ScriptLogManager::ScriptLogManager(QString filePath, int capacity)
    : m_filePath(std::move(filePath))
    , m_capacity(qMax(100, capacity))
{
}

void ScriptLogManager::setCapacity(int capacity)
{
    m_capacity = qMax(100, capacity);
    while (m_entries.size() > m_capacity)
        m_entries.removeFirst();
}

void ScriptLogManager::loadExisting()
{
    m_entries.clear();
    if (m_filePath.isEmpty())
        return;
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QList<QByteArray> lines = file.readAll().split('\n');
    const int start = qMax(0, lines.size() - m_capacity);
    for (int i = start; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        if (line.isEmpty())
            continue;
        const QJsonObject object = QJsonDocument::fromJson(line).object();
        if (object.isEmpty())
            continue;
        ScriptLogEntry entry;
        entry.time = QDateTime::fromString(object.value(QStringLiteral("time")).toString(),
                                           Qt::ISODateWithMs);
        entry.scriptId = object.value(QStringLiteral("scriptId")).toString();
        entry.scriptName = object.value(QStringLiteral("script")).toString();
        entry.message = object.value(QStringLiteral("message")).toString();
        const QString level = object.value(QStringLiteral("level")).toString();
        if (level == QStringLiteral("DEBUG"))
            entry.level = ScriptLogLevel::Debug;
        else if (level == QStringLiteral("WARN"))
            entry.level = ScriptLogLevel::Warning;
        else if (level == QStringLiteral("ERROR"))
            entry.level = ScriptLogLevel::Error;
        if (entry.time.isValid() && !entry.message.isEmpty())
            m_entries.append(entry);
    }
}

void ScriptLogManager::append(ScriptLogLevel level, const QString& scriptId,
                              const QString& scriptName, const QString& message)
{
    ScriptLogEntry entry;
    entry.time = QDateTime::currentDateTime();
    entry.level = level;
    entry.scriptId = scriptId;
    entry.scriptName = scriptName;
    entry.message = message;
    append(entry);
}

void ScriptLogManager::append(const ScriptLogEntry& entry)
{
    m_entries.append(entry);
    while (m_entries.size() > m_capacity)
        m_entries.removeFirst();

    if (m_filePath.isEmpty())
        return;
    const QFileInfo info(m_filePath);
    if (!info.absolutePath().isEmpty())
        QDir().mkpath(info.absolutePath());
    rotateIfNeeded();

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QJsonObject object;
    object.insert(QStringLiteral("time"), entry.time.toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("level"), logLevelLabel(entry.level));
    object.insert(QStringLiteral("scriptId"), entry.scriptId);
    object.insert(QStringLiteral("script"), entry.scriptName);
    object.insert(QStringLiteral("message"), entry.message);
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.write("\n");
}

void ScriptLogManager::rotateIfNeeded()
{
    if (m_filePath.isEmpty())
        return;
    if (QFileInfo(m_filePath).size() < kMaxLogFileBytes)
        return;
    const QString backup = m_filePath + QStringLiteral(".1");
    QFile::remove(backup);
    QFile::rename(m_filePath, backup);
}

void ScriptLogManager::recordRun(const QString& profileName, const ScriptStepResult& step)
{
    QString message;
    if (step.ok) {
        message = QStringLiteral("执行成功 · %1 ms · 修改 %2 处")
                      .arg(step.durationMs)
                      .arg(step.diff.total);
    } else if (step.skipped) {
        message = QStringLiteral("执行失败但按策略跳过 · %1").arg(step.errorMessage);
    } else {
        message = QStringLiteral("执行失败 · %1").arg(step.errorMessage);
        if (step.line > 0)
            message += QStringLiteral("（行 %1）").arg(step.line);
    }
    if (!profileName.isEmpty())
        message = QStringLiteral("[%1] %2").arg(profileName, message);
    append(step.ok ? ScriptLogLevel::Info : ScriptLogLevel::Error, step.scriptId, step.name,
           message);
    for (const ScriptLogLine& line : step.logs)
        append(line.level, step.scriptId, step.name, line.message);
}

void ScriptLogManager::recordUpdate(const ScriptUpdateResult& update)
{
    QString message;
    if (update.ok && update.notModified)
        message = QStringLiteral("远程脚本没有变化（HTTP 304）");
    else if (update.ok && update.skipped)
        message = QStringLiteral("脚本内容未变化，跳过版本写入");
    else if (update.ok)
        message = QStringLiteral("远程脚本已更新 · %1 字节 · %2")
                      .arg(update.bytes)
                      .arg(update.newSha256.left(12));
    else
        message = QStringLiteral("远程脚本更新失败：%1").arg(update.error);
    append(update.ok ? ScriptLogLevel::Info : ScriptLogLevel::Warning, update.scriptId,
           update.scriptName, message);
}

QVector<ScriptLogEntry> ScriptLogManager::entriesFor(const QString& scriptId) const
{
    QVector<ScriptLogEntry> result;
    for (const ScriptLogEntry& entry : m_entries) {
        if (entry.scriptId == scriptId)
            result.append(entry);
    }
    return result;
}

void ScriptLogManager::clear()
{
    m_entries.clear();
    if (!m_filePath.isEmpty()) {
        QFile::remove(m_filePath);
        QFile::remove(m_filePath + QStringLiteral(".1"));
    }
}

}  // namespace nq::scripts
