#include "scripts/MihomoConfigValidator.h"

#include <QFileInfo>

namespace nq::scripts {
namespace {
QString compactOutput(const QByteArray& raw)
{
    QString text = QString::fromUtf8(raw).trimmed();
    if (text.size() > 4000)
        text = QStringLiteral("…") + text.right(4000);
    return text;
}
}  // namespace

MihomoConfigValidator::MihomoConfigValidator(QObject* parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_timeoutTimer.setSingleShot(true);
    connect(&m_timeoutTimer, &QTimer::timeout, this, &MihomoConfigValidator::handleTimeout);
    connect(&m_process, &QProcess::readyRead, this, [this] {
        m_output += m_process.readAll();
    });
    connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
                if (!m_running)
                    return;
                const QByteArray remaining = m_process.readAll();
                m_output += remaining;
                if (m_canceled) {
                    finish(false, QStringLiteral("配置检测已取消"));
                    return;
                }
                if (status == QProcess::CrashExit) {
                    finish(false, QStringLiteral("Mihomo 配置检测进程异常退出"));
                    return;
                }
                if (exitCode == 0) {
                    finish(true, QStringLiteral("Mihomo 配置检测通过"));
                    return;
                }
                QString detail = compactOutput(m_output);
                if (detail.isEmpty())
                    detail = QStringLiteral("Mihomo 以退出码 %1 拒绝了配置").arg(exitCode);
                finish(false, QStringLiteral("Mihomo 配置检测失败：%1").arg(detail));
            });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_running)
            return;
        if (error == QProcess::FailedToStart)
            finish(false, QStringLiteral("无法启动 Mihomo 配置检测：%1").arg(m_process.errorString()));
        else if (error == QProcess::Crashed)
            finish(false, QStringLiteral("Mihomo 配置检测进程崩溃"));
    });
}

void MihomoConfigValidator::validate(const QString& corePath, const QString& configPath,
                                     const QString& dataDirectory, int timeoutMs)
{
    if (m_running) {
        emit finished(false, QStringLiteral("已有配置检测任务正在进行"));
        return;
    }
    if (!QFileInfo::exists(corePath)) {
        emit finished(false, QStringLiteral("找不到 Mihomo 核心：%1").arg(corePath));
        return;
    }
    if (!QFileInfo::exists(configPath)) {
        emit finished(false, QStringLiteral("找不到待检测的配置文件"));
        return;
    }

    m_running = true;
    m_canceled = false;
    m_output.clear();
    m_configPath = configPath;
    m_process.setProgram(corePath);
    m_process.setArguments({QStringLiteral("-t"), QStringLiteral("-d"), dataDirectory,
                            QStringLiteral("-f"), configPath});
    m_process.start();
    m_timeoutTimer.start(qBound(3000, timeoutMs, 120000));
}

void MihomoConfigValidator::handleTimeout()
{
    if (!m_running)
        return;
    m_output += m_process.readAll();
    m_process.kill();
    finish(false, QStringLiteral("Mihomo 配置检测超时（%1 秒）")
                      .arg(qMax(3, m_timeoutTimer.interval() / 1000)));
}

void MihomoConfigValidator::cancel()
{
    if (!m_running)
        return;
    m_canceled = true;
    m_process.kill();
}

void MihomoConfigValidator::finish(bool valid, const QString& message)
{
    if (!m_running)
        return;
    m_running = false;
    m_timeoutTimer.stop();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1500);
    }
    m_output.clear();
    emit finished(valid, message);
}

bool MihomoConfigValidator::validateSync(const QString& corePath, const QString& configPath,
                                         const QString& dataDirectory, int timeoutMs,
                                         QString* message)
{
    if (message)
        message->clear();
    if (!QFileInfo::exists(corePath)) {
        if (message)
            *message = QStringLiteral("找不到 Mihomo 核心：%1").arg(corePath);
        return false;
    }
    if (!QFileInfo::exists(configPath)) {
        if (message)
            *message = QStringLiteral("找不到待检测的配置文件");
        return false;
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setProgram(corePath);
    process.setArguments({QStringLiteral("-t"), QStringLiteral("-d"), dataDirectory,
                          QStringLiteral("-f"), configPath});
    process.start();
    if (!process.waitForStarted(5000)) {
        if (message)
            *message = QStringLiteral("无法启动 Mihomo 配置检测：%1").arg(process.errorString());
        return false;
    }
    if (!process.waitForFinished(qBound(3000, timeoutMs, 120000))) {
        process.kill();
        process.waitForFinished(1500);
        if (message)
            *message = QStringLiteral("Mihomo 配置检测超时");
        return false;
    }
    const QByteArray output = process.readAll();
    if (process.exitStatus() == QProcess::CrashExit) {
        if (message)
            *message = QStringLiteral("Mihomo 配置检测进程异常退出");
        return false;
    }
    if (process.exitCode() != 0) {
        if (message) {
            const QString detail = compactOutput(output);
            *message = detail.isEmpty()
                           ? QStringLiteral("Mihomo 以退出码 %1 拒绝了配置")
                                 .arg(process.exitCode())
                           : QStringLiteral("Mihomo 配置检测失败：%1").arg(detail);
        }
        return false;
    }
    if (message)
        *message = QStringLiteral("Mihomo 配置检测通过");
    return true;
}

}  // namespace nq::scripts
