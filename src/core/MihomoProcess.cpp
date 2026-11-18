#include "core/MihomoProcess.h"

#include "service/ServiceClient.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QString stripAnsi(QString text)
{
    static const QRegularExpression escape(
        QStringLiteral("\\x1B\\[[0-9;?]*[ -/]*[@-~]"));
    text.remove(escape);
    return text.trimmed();
}
}  // namespace

MihomoProcess::MihomoProcess(QObject* parent)
    : QObject(parent)
{
#ifdef Q_OS_WIN
    m_jobHandle = CreateJobObjectW(nullptr, nullptr);
    if (m_jobHandle) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(static_cast<HANDLE>(m_jobHandle),
                                JobObjectExtendedLimitInformation, &info, sizeof(info));
    }
#endif

    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_process.setReadChannel(QProcess::StandardOutput);

    m_serviceTimer.setInterval(1000);
    m_serviceNetwork.setProxy(QNetworkProxy::NoProxy);
    connect(&m_serviceTimer, &QTimer::timeout, this, &MihomoProcess::pollServiceStatus);

    m_killTimer.setSingleShot(true);
    m_killTimer.setInterval(6000);
    connect(&m_killTimer, &QTimer::timeout, this, [this] {
        if (m_process.state() != QProcess::NotRunning) {
            emit logLine(QStringLiteral(
                "level=warning msg=\"Mihomo 优雅退出超时，正在强制终止\""));
            m_process.kill();
        }
    });

    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] { consumeOutput(); });
    connect(&m_process, &QProcess::started, this, [this] {
#ifdef Q_OS_WIN
        if (m_jobHandle) {
            HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE | SYNCHRONIZE,
                                         FALSE, static_cast<DWORD>(m_process.processId()));
            if (process) {
                AssignProcessToJobObject(static_cast<HANDLE>(m_jobHandle), process);
                CloseHandle(process);
            }
        }
#endif
        emit started();
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit failed(QStringLiteral("无法启动 Mihomo：%1").arg(m_process.errorString()));
        } else if (error == QProcess::Crashed) {
            emit failed(QStringLiteral("Mihomo 核心已崩溃"));
        }
    });
    connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
                m_killTimer.stop();
                consumeOutput(true);
                const bool crashed = status == QProcess::CrashExit;
                const bool wasStopping = m_stopping;
                m_stopping = false;
                if (!crashed && exitCode != 0 && !wasStopping) {
                    emit failed(QStringLiteral("Mihomo 已退出（代码 %1）").arg(exitCode));
                }
                emit finished(exitCode, crashed);
            });
#ifdef Q_OS_WIN
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        // A GUI launch (installer, Explorer, shortcut) has no console. Give
        // Mihomo its own hidden console so CTRL_BREAK_EVENT remains available
        // without allocating a visible console in the parent process.
        args->flags &= ~CREATE_NO_WINDOW;
        args->flags |= CREATE_NEW_PROCESS_GROUP;
        if (!GetConsoleWindow()) {
            args->flags |= CREATE_NEW_CONSOLE;
            if (args->startupInfo) {
                args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
                args->startupInfo->wShowWindow = SW_HIDE;
            }
        }
    });
#endif
}

MihomoProcess::~MihomoProcess()
{
    cancelServiceProbe();
    closeServiceProcessHandle();
#ifdef Q_OS_WIN
    if (m_jobHandle) {
        CloseHandle(static_cast<HANDLE>(m_jobHandle));
        m_jobHandle = nullptr;
    }
#endif
}

void MihomoProcess::setProgram(const QString& path)
{
    m_program = QDir::toNativeSeparators(path);
}

void MihomoProcess::setDataDirectory(const QString& path)
{
    m_dataDirectory = path;
}

void MihomoProcess::setServiceMode(bool enabled)
{
    if (m_serviceMode == enabled)
        return;
    if (isRunning())
        stop();
    if (isRunning())
        return;
    cancelServiceProbe();
    closeServiceProcessHandle();
    m_serviceMode = enabled;
    m_serviceRunning = false;
    m_servicePid = 0;
    m_serviceError.clear();
}

qint64 MihomoProcess::processId() const
{
    return m_serviceMode ? m_servicePid : m_process.processId();
}

bool MihomoProcess::isRunning() const
{
    return m_serviceMode ? m_serviceRunning
                         : m_process.state() != QProcess::NotRunning;
}

void MihomoProcess::start(const QString& configPath, int controllerPort, const QString& secret)
{
    if (isRunning()) {
        emit failed(QStringLiteral("Mihomo 已经在运行"));
        return;
    }
    if (!m_serviceMode && !QFileInfo::exists(m_program)) {
        emit failed(QStringLiteral("找不到 Mihomo 可执行文件：%1").arg(m_program));
        return;
    }
    if (!QFileInfo::exists(configPath)) {
        emit failed(QStringLiteral("找不到运行时配置：%1").arg(configPath));
        return;
    }

    m_buffer.clear();
    m_stopping = false;
    QDir().mkpath(m_dataDirectory);

    if (m_serviceMode) {
        cancelServiceProbe();
        closeServiceProcessHandle();
        m_serviceLogSeq = 0;
        m_servicePid = 0;
        m_serviceWorkingSet = 0;
        m_serviceMemoryValid = false;
        m_serviceStatusFailures = 0;
        m_helperReportedStopped = false;
        m_serviceControllerPort = controllerPort;
        m_serviceSecret = secret;
        m_serviceWarningTimer.invalidate();
        m_serviceVerificationLogTimer.invalidate();
        m_serviceTimer.setInterval(1000);
        m_serviceError.clear();
        QString error;
        if (!nq::service::ServiceClient::start(configPath, m_dataDirectory, controllerPort,
                                               secret, &error)) {
            m_serviceRunning = false;
            emit failed(error.isEmpty() ? QStringLiteral("Helper 服务无法启动 Mihomo") : error);
            return;
        }
        m_serviceRunning = true;
        m_serviceTimer.start();
        emit started();
        return;
    }

    m_process.setWorkingDirectory(m_dataDirectory);
    m_process.setProgram(m_program);
    m_process.setArguments({
        QStringLiteral("-d"),
        QDir::toNativeSeparators(m_dataDirectory),
        QStringLiteral("-f"),
        QDir::toNativeSeparators(configPath),
        QStringLiteral("-ext-ctl"),
        QStringLiteral("127.0.0.1:%1").arg(controllerPort),
        QStringLiteral("-secret"),
        secret,
    });

    m_process.start();
}

void MihomoProcess::stop()
{
    if (!isRunning())
        return;
    m_stopping = true;
    if (m_serviceMode) {
        m_serviceTimer.stop();
        cancelServiceProbe();
        QString error;
        if (!nq::service::ServiceClient::stop(&error)) {
            emit logLine(QStringLiteral("warning\t%1").arg(error));
            m_stopping = false;
            m_serviceTimer.start();
            return;
        }
        closeServiceProcessHandle();
        m_serviceRunning = false;
        m_servicePid = 0;
        m_serviceWorkingSet = 0;
        m_serviceMemoryValid = false;
        emit finished(0, false);
        return;
    }
#ifdef Q_OS_WIN
    if (sendCtrlBreak()) {
        emit logLine(QStringLiteral("info\t已请求 Mihomo 优雅退出（CTRL_BREAK_EVENT）"));
        m_killTimer.start(6000);
        return;
    }
    emit logLine(QStringLiteral("warning\t无法发送控制台退出信号，回退为终止进程"));
    m_killTimer.start(1800);
#else
    m_killTimer.start();
#endif
    m_process.terminate();
}

#ifdef Q_OS_WIN
bool MihomoProcess::sendCtrlBreak()
{
    const DWORD pid = static_cast<DWORD>(m_process.processId());
    if (pid == 0)
        return false;

    if (GetConsoleWindow())
        return GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid) != 0;

    // A GUI launch (installer, Explorer, shortcut) gives Mihomo its own hidden
    // console. Attach to it only long enough to deliver CTRL_BREAK_EVENT, then
    // detach again so Nyan-Q never owns a visible console window.
    if (!AttachConsole(pid))
        return false;

    SetConsoleCtrlHandler(nullptr, TRUE);
    const bool sent = GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid) != 0;
    SetConsoleCtrlHandler(nullptr, FALSE);
    FreeConsole();
    return sent;
}
#endif

bool MihomoProcess::stopAndWait(int timeoutMs)
{
    if (!isRunning())
        return true;
    m_stopping = true;
    m_killTimer.stop();
    if (m_serviceMode) {
        m_serviceTimer.stop();
        cancelServiceProbe();
        QString error;
        const bool stopped = nq::service::ServiceClient::stop(&error);
        if (!stopped) {
            emit logLine(QStringLiteral("warning\t停止内核未获确认，将继续核验状态：%1").arg(error));
            m_stopping = false;
            m_serviceTimer.start();
            return false;
        }
        closeServiceProcessHandle();
        m_serviceRunning = false;
        m_servicePid = 0;
        m_serviceWorkingSet = 0;
        m_serviceMemoryValid = false;
        return true;
    }
#ifdef Q_OS_WIN
    const bool graceful = sendCtrlBreak();
    if (graceful)
        emit logLine(QStringLiteral("info\t已请求 Mihomo 优雅退出（CTRL_BREAK_EVENT）"));
    else {
        emit logLine(QStringLiteral("warning\t无法发送控制台退出信号，回退为终止进程"));
        m_process.terminate();
    }
    if (m_process.waitForFinished(timeoutMs))
        return true;
    m_process.terminate();
#else
    m_process.terminate();
    if (m_process.waitForFinished(timeoutMs))
        return true;
#endif
    if (!m_process.waitForFinished(800)) {
        m_process.kill();
        m_process.waitForFinished(1200);
    }
    return !isRunning();
}

void MihomoProcess::pollServiceStatus()
{
    if (!m_serviceMode || !m_serviceRunning || m_stopping)
        return;
    QString error;
    QJsonObject status =
        nq::service::ServiceClient::status(m_serviceLogSeq, &error);
    if (!status.isEmpty() &&
        (!status.value(QStringLiteral("running")).isBool() ||
         status.value(QStringLiteral("protocol")).toInt() != nq::service::kProtocolVersion)) {
        error = QStringLiteral("Helper 状态响应无效（command=status stage=validate，缺少运行状态或协议不匹配）");
        status = {};
    }
    if (status.isEmpty()) {
        m_helperReportedStopped = false;
        m_serviceStatusFailures = qMin(m_serviceStatusFailures + 1, 1000000);
        m_serviceMemoryValid = false;
        m_serviceTimer.setInterval(qMin(5000, 1000 * (1 + qMin(m_serviceStatusFailures, 4))));
        if (!m_serviceWarningTimer.isValid() || m_serviceWarningTimer.elapsed() >= 15000) {
            emit logLine(QStringLiteral("level=warning Helper 通信暂时中断，正在自动重连并核验内核"
                                       "（连续失败 %1 次，下次重连 %2 ms）：%3")
                             .arg(m_serviceStatusFailures).arg(m_serviceTimer.interval()).arg(error));
            m_serviceWarningTimer.restart();
        }
        verifyServiceKernel();
        return;
    }
    if (status.value(QStringLiteral("running")).toBool())
        cancelServiceProbe();
    if (m_serviceStatusFailures > 0) {
        emit logLine(QStringLiteral("level=info Helper 通信已恢复（此前连续失败 %1 次）")
                         .arg(m_serviceStatusFailures));
    }
    m_serviceStatusFailures = 0;
    m_serviceTimer.setInterval(1000);
    m_serviceWarningTimer.invalidate();
    m_helperReportedStopped = !status.value(QStringLiteral("running")).toBool();

    const QJsonArray logs = status.value(QStringLiteral("logs")).toArray();
    for (const QJsonValue& value : logs) {
        const QString line = value.toString();
        if (!line.isEmpty())
            emit logLine(line);
    }
    m_serviceLogSeq =
        static_cast<quint64>(status.value(QStringLiteral("next")).toDouble());
    const qint64 pid = static_cast<qint64>(status.value(QStringLiteral("pid")).toDouble());
    if (pid > 0 && pid != m_servicePid) {
        closeServiceProcessHandle();
        m_servicePid = pid;
    }
    if (m_servicePid > 0) {
        QString processDetail;
        serviceProcessState(&processDetail); // Retain the handle before any PID reuse.
    }
    m_serviceWorkingSet =
        static_cast<qint64>(status.value(QStringLiteral("memory")).toDouble());
    m_serviceMemoryValid = status.value(QStringLiteral("memoryValid")).toBool();

    const QString serviceError = status.value(QStringLiteral("error")).toString();
    m_serviceError = serviceError;

    if (m_helperReportedStopped) {
        verifyServiceKernel();
    } else {
        m_serviceVerificationLogTimer.invalidate();
    }
}

void MihomoProcess::cancelServiceProbe()
{
    if (!m_serviceProbe)
        return;
    auto* reply = m_serviceProbe;
    m_serviceProbe = nullptr;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}

void MihomoProcess::closeServiceProcessHandle()
{
#ifdef Q_OS_WIN
    if (m_serviceProcessHandle)
        CloseHandle(static_cast<HANDLE>(m_serviceProcessHandle));
#endif
    m_serviceProcessHandle = nullptr;
}

MihomoProcess::ServiceProcessState MihomoProcess::serviceProcessState(QString* detail)
{
#ifdef Q_OS_WIN
    if (m_servicePid > 0) {
        if (!m_serviceProcessHandle) {
            m_serviceProcessHandle = OpenProcess(SYNCHRONIZE, FALSE,
                                                static_cast<DWORD>(m_servicePid));
            if (!m_serviceProcessHandle) {
                const DWORD code = GetLastError();
                *detail = QStringLiteral("pid=%1 OpenProcess error=%2").arg(m_servicePid).arg(code);
                // Access denied is unknown, never evidence of process exit.
                return code == ERROR_INVALID_PARAMETER ? ServiceProcessState::Exited
                                                       : ServiceProcessState::Unknown;
            }
        }
        const DWORD state = WaitForSingleObject(static_cast<HANDLE>(m_serviceProcessHandle), 0);
        *detail = QStringLiteral("pid=%1 wait=%2").arg(m_servicePid).arg(state);
        if (state == WAIT_OBJECT_0)
            return ServiceProcessState::Exited;
        if (state == WAIT_TIMEOUT)
            return ServiceProcessState::Running;
        *detail += QStringLiteral(" error=%1").arg(GetLastError());
        return ServiceProcessState::Unknown;
    }
#endif
    *detail = QStringLiteral("pid=%1，进程状态未知").arg(m_servicePid);
    return ServiceProcessState::Unknown;
}

void MihomoProcess::verifyServiceKernel()
{
    if (m_serviceProbe || !m_serviceRunning || m_stopping)
        return;
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/version")
                                    .arg(m_serviceControllerPort)));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_serviceSecret).toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(1500);
    auto* reply = m_serviceNetwork.get(request);
    m_serviceProbe = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_serviceProbe != reply)
            return;
        m_serviceProbe = nullptr;
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto object = QJsonDocument::fromJson(reply->isOpen() ? reply->readAll() : QByteArray{}).object();
        const bool controllerAlive = reply->error() == QNetworkReply::NoError && httpStatus == 200 &&
                                    !object.value(QStringLiteral("version")).toString().isEmpty();
        const QString controllerDetail = QStringLiteral("controller=%1 http=%2 networkError=%3 detail=%4")
            .arg(controllerAlive ? QStringLiteral("alive") : QStringLiteral("unavailable"))
            .arg(httpStatus).arg(static_cast<int>(reply->error())).arg(reply->errorString());
        reply->deleteLater();
        QString processDetail;
        const ServiceProcessState state = serviceProcessState(&processDetail);
        const QString evidence = controllerDetail + QStringLiteral("; ") + processDetail;
        if (!controllerAlive &&
            (state == ServiceProcessState::Exited ||
             (m_helperReportedStopped && state != ServiceProcessState::Running))) {
            finishServiceProcess(!m_helperReportedStopped,
                QStringLiteral("已核验 Mihomo 核心退出：%1").arg(evidence));
            return;
        }
        if (!m_serviceVerificationLogTimer.isValid() || m_serviceVerificationLogTimer.elapsed() >= 15000) {
            emit logLine(QStringLiteral("level=warning 内核状态核验：%1；保留运行状态并继续重连 Helper")
                             .arg(evidence));
            m_serviceVerificationLogTimer.restart();
        }
    });
}

void MihomoProcess::finishServiceProcess(bool confirmedCrash, const QString& reason)
{
    m_serviceTimer.stop();
    cancelServiceProbe();
    closeServiceProcessHandle();
    m_serviceRunning = false;
    m_servicePid = 0;
    m_serviceWorkingSet = 0;
    m_serviceMemoryValid = false;
    emit logLine(QStringLiteral("level=warning %1").arg(reason));
    if (confirmedCrash || !m_serviceError.isEmpty())
        emit failed(m_serviceError.isEmpty() ? reason : m_serviceError);
    emit finished(confirmedCrash ? 1 : 0, confirmedCrash);
}

void MihomoProcess::consumeOutput(bool flush)
{
    m_buffer += m_process.readAllStandardOutput();
    int newline = m_buffer.indexOf('\n');
    while (newline >= 0) {
        QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        forwardLine(QString::fromUtf8(line));
        newline = m_buffer.indexOf('\n');
    }
    if (flush && !m_buffer.isEmpty()) {
        forwardLine(QString::fromUtf8(m_buffer));
        m_buffer.clear();
    }
}

void MihomoProcess::forwardLine(const QString& raw)
{
    const QString line = stripAnsi(raw);
    if (!line.isEmpty())
        emit logLine(line);
}
