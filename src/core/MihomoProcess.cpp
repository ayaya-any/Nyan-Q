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
        if (GetConsoleWindow()) {
            // Qt would otherwise add CREATE_NO_WINDOW for GUI parents, which
            // leaves the child without a console and prevents CTRL_BREAK.
            args->flags &= ~CREATE_NO_WINDOW;
            args->flags |= CREATE_NEW_PROCESS_GROUP;
        }
    });
#endif
}

bool MihomoProcess::ensureConsoleForGracefulStop()
{
#ifdef Q_OS_WIN
    static const bool prepared = [] {
        if (GetConsoleWindow())
            return true;

        const HANDLE savedInput = GetStdHandle(STD_INPUT_HANDLE);
        const HANDLE savedOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        const HANDLE savedError = GetStdHandle(STD_ERROR_HANDLE);

        if (!AllocConsole())
            return false;

        if (const HWND console = GetConsoleWindow())
            ShowWindow(console, SW_HIDE);

        // Keep the process' inherited stdio usable (e.g. --smoke-test output).
        if (savedInput && savedInput != INVALID_HANDLE_VALUE)
            SetStdHandle(STD_INPUT_HANDLE, savedInput);
        if (savedOutput && savedOutput != INVALID_HANDLE_VALUE)
            SetStdHandle(STD_OUTPUT_HANDLE, savedOutput);
        if (savedError && savedError != INVALID_HANDLE_VALUE)
            SetStdHandle(STD_ERROR_HANDLE, savedError);
        return true;
    }();
    return prepared;
#else
    return true;
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
    ensureConsoleForGracefulStop();
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
    if (GetConsoleWindow() &&
        GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT,
                                 static_cast<DWORD>(m_process.processId()))) {
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

bool MihomoProcess::stopAndWait(int timeoutMs)
{
    if (!isRunning())
        return true;
    m_stopping = true;
    m_killTimer.stop();
#ifdef Q_OS_WIN
    bool graceful = false;
    if (GetConsoleWindow() &&
        GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT,
                                 static_cast<DWORD>(m_process.processId()))) {
        emit logLine(QStringLiteral("info\t已请求 Mihomo 优雅退出（CTRL_BREAK_EVENT）"));
        graceful = true;
    }
    if (!graceful) {
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
