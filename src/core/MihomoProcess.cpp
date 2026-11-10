#include "core/MihomoProcess.h"

#include <QDir>
#include <QFileInfo>
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

qint64 MihomoProcess::processId() const
{
    return m_process.processId();
}

bool MihomoProcess::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

void MihomoProcess::start(const QString& configPath, int controllerPort, const QString& secret)
{
    if (isRunning()) {
        emit failed(QStringLiteral("Mihomo 已经在运行"));
        return;
    }
    if (!QFileInfo::exists(m_program)) {
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
