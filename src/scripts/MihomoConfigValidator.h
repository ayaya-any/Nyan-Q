#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

namespace nq::scripts {

// Runs `mihomo -t -d <data-dir> -f <config>` without starting listeners. This
// is the authoritative final-configuration check; a successful YAML parse in
// Nyan-Q is not considered sufficient on its own.
class MihomoConfigValidator : public QObject {
    Q_OBJECT

public:
    explicit MihomoConfigValidator(QObject* parent = nullptr);

    void validate(const QString& corePath, const QString& configPath,
                  const QString& dataDirectory, int timeoutMs = 20000);
    void cancel();
    bool isBusy() const { return m_running; }

    static bool validateSync(const QString& corePath, const QString& configPath,
                             const QString& dataDirectory, int timeoutMs, QString* message);

signals:
    void finished(bool valid, const QString& message);

private:
    void finish(bool valid, const QString& message);
    void handleTimeout();

    QProcess m_process;
    QTimer m_timeoutTimer;
    QByteArray m_output;
    QString m_configPath;
    bool m_running = false;
    bool m_canceled = false;
};

}  // namespace nq::scripts
