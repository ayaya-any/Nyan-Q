#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

class QNetworkReply;

class MihomoProcess final : public QObject {
    Q_OBJECT

public:
    explicit MihomoProcess(QObject* parent = nullptr);
    ~MihomoProcess() override;

    void setProgram(const QString& path);
    void setDataDirectory(const QString& path);
    void setServiceMode(bool enabled);
    bool serviceMode() const { return m_serviceMode; }
    qint64 workingSetBytes() const { return m_serviceWorkingSet; }
    bool workingSetValid() const { return m_serviceMemoryValid; }
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
    void pollServiceStatus();
    void verifyServiceKernel();
    void cancelServiceProbe();
    void closeServiceProcessHandle();
    enum class ServiceProcessState { Running, Exited, Unknown };
    ServiceProcessState serviceProcessState(QString* detail);
    void finishServiceProcess(bool confirmedCrash, const QString& reason);
#ifdef Q_OS_WIN
    bool sendCtrlBreak();
#endif

    QProcess m_process;
    QTimer m_killTimer;
    QTimer m_serviceTimer;
    QString m_program;
    QString m_dataDirectory;
    QByteArray m_buffer;
    void* m_jobHandle = nullptr;
    bool m_stopping = false;
    bool m_serviceMode = false;
    bool m_serviceRunning = false;
    qint64 m_servicePid = 0;
    quint64 m_serviceLogSeq = 0;
    qint64 m_serviceWorkingSet = 0;
    bool m_serviceMemoryValid = false;
    QString m_serviceError;
    int m_serviceStatusFailures = 0;
    bool m_helperReportedStopped = false;
    int m_serviceControllerPort = 0;
    QString m_serviceSecret;
    QNetworkAccessManager m_serviceNetwork;
    QNetworkReply* m_serviceProbe = nullptr;
    void* m_serviceProcessHandle = nullptr;
    QElapsedTimer m_serviceWarningTimer;
    QElapsedTimer m_serviceVerificationLogTimer;
};
