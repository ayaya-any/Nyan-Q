#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

class MihomoProcess final : public QObject {
    Q_OBJECT

public:
    explicit MihomoProcess(QObject* parent = nullptr);
    ~MihomoProcess() override;

    // Attaches (or allocates and hides) a console so Windows CTRL_BREAK_EVENT
    // can be delivered to Mihomo, which handles SIGINT/SIGTERM gracefully.
    static bool ensureConsoleForGracefulStop();

    void setProgram(const QString& path);
    void setDataDirectory(const QString& path);
    QString program() const { return m_program; }
    qint64 processId() const;
    bool isRunning() const;

    void start(const QString& configPath, int controllerPort, const QString& secret);
    void stop();
    bool stopAndWait(int timeoutMs = 2500);

signals:
    void started();
    void finished(int exitCode, bool crashed);
    void failed(const QString& message);
    void logLine(const QString& line);

private:
    void consumeOutput(bool flush = false);
    void forwardLine(const QString& line);

    QProcess m_process;
    QTimer m_killTimer;
    QString m_program;
    QString m_dataDirectory;
    QByteArray m_buffer;
    void* m_jobHandle = nullptr;
    bool m_stopping = false;
};
