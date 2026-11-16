#include "service/ServiceRuntime.h"

#include "service/ServiceProtocol.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace nq::service {

ServiceRuntime::ServiceRuntime(QObject* parent)
    : QObject(parent)
{
    connect(&m_process, &MihomoProcess::started, this, [this] {
        m_started = true;
        m_error.clear();
    });
    connect(&m_process, &MihomoProcess::failed, this, [this](const QString& message) {
        m_error = message;
        appendLog(message);
    });
    connect(&m_process, &MihomoProcess::finished, this, [this](int exitCode, bool crashed) {
        m_started = false;
        if (crashed || exitCode != 0)
            m_error = QStringLiteral("Mihomo 已退出（代码 %1）").arg(exitCode);
    });
    connect(&m_process, &MihomoProcess::logLine, this, &ServiceRuntime::appendLog);
}

bool ServiceRuntime::start(const QString& configPath, const QString& dataDir, int controllerPort,
                           const QString& secret, QString* error)
{
    if (configPath.isEmpty() || dataDir.isEmpty()) {
        if (error)
            *error = QStringLiteral("缺少配置或数据目录");
        return false;
    }
    if (!QFile::exists(configPath)) {
        if (error)
            *error = QStringLiteral("找不到运行配置：%1").arg(configPath);
        return false;
    }
    const QString dataRoot = QDir(dataDir).absolutePath();
    const QString configRoot = QFileInfo(configPath).absolutePath();
    if (!configRoot.startsWith(dataRoot + QLatin1Char('/'), Qt::CaseInsensitive) &&
        configRoot.compare(dataRoot, Qt::CaseInsensitive) != 0) {
        if (error)
            *error = QStringLiteral("运行配置必须位于 Nyan-Q 数据目录内");
        return false;
    }
    if (!QFile::exists(corePath())) {
        if (error)
            *error = QStringLiteral("安装目录中缺少 Mihomo 核心");
        return false;
    }

    if (m_process.isRunning())
        m_process.stopAndWait(4000);

    m_logs.clear();
    m_logBase = 0;
    m_logNext = 0;
    m_error.clear();
    m_logPath = QDir(dataDir).filePath(QStringLiteral("runtime/core.log"));
    QDir().mkpath(QFileInfo(m_logPath).absolutePath());
    QFile::remove(m_logPath);

    m_process.setProgram(corePath());
    m_process.setDataDirectory(dataDir);
    m_process.start(configPath, controllerPort, secret);
    return true;
}

bool ServiceRuntime::stop(QString* error)
{
    const bool stopped = m_process.stopAndWait(5000);
    if (!stopped && error)
        *error = QStringLiteral("Mihomo 核心未能及时退出");
    m_started = false;
    return stopped;
}

QJsonObject ServiceRuntime::status(quint64 since) const
{
    QJsonObject result;
    result.insert(QStringLiteral("running"), m_process.isRunning());
    result.insert(QStringLiteral("pid"), static_cast<double>(m_process.processId()));
    result.insert(QStringLiteral("error"), m_error);

    qint64 workingSet = 0;
    bool memoryValid = false;
#ifdef Q_OS_WIN
    const qint64 pid = m_process.processId();
    if (pid > 0) {
        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                                     static_cast<DWORD>(pid));
        if (process) {
            PROCESS_MEMORY_COUNTERS_EX counters = {};
            if (GetProcessMemoryInfo(process,
                                     reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                     sizeof(counters))) {
                workingSet = static_cast<qint64>(counters.WorkingSetSize);
                memoryValid = true;
            }
            CloseHandle(process);
        }
    }
#endif
    result.insert(QStringLiteral("memory"), static_cast<double>(workingSet));
    result.insert(QStringLiteral("memoryValid"), memoryValid);

    const quint64 begin = since < m_logBase ? 0 : since - m_logBase;
    QJsonArray logs;
    for (quint64 i = begin; i < static_cast<quint64>(m_logs.size()); ++i)
        logs.append(m_logs.at(static_cast<int>(i)));
    result.insert(QStringLiteral("logs"), logs);
    result.insert(QStringLiteral("next"), static_cast<double>(m_logBase + m_logs.size()));
    return result;
}

void ServiceRuntime::appendLog(const QString& line)
{
    if (line.isEmpty())
        return;
    m_logs.append(line);
    if (m_logs.size() > 500) {
        const int remove = m_logs.size() - 500;
        m_logs.remove(0, remove);
        m_logBase += static_cast<quint64>(remove);
    }
    ++m_logNext;
    appendLogFile(line);
}

void ServiceRuntime::appendLogFile(const QString& line)
{
    if (m_logPath.isEmpty())
        return;
    QFile file(m_logPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write(line.toUtf8());
        file.write("\n");
    }
}

}  // namespace nq::service
