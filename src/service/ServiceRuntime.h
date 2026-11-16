#pragma once

#include "core/MihomoProcess.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

namespace nq::service {

class ServiceRuntime final : public QObject {
    Q_OBJECT

public:
    explicit ServiceRuntime(QObject* parent = nullptr);

    bool start(const QString& configPath, const QString& dataDir, int controllerPort,
               const QString& secret, QString* error);
    bool stop(QString* error = nullptr);
    QJsonObject status(quint64 since) const;

private:
    void appendLog(const QString& line);
    void appendLogFile(const QString& line);

    MihomoProcess m_process;
    QVector<QString> m_logs;
    quint64 m_logBase = 0;
    quint64 m_logNext = 0;
    QString m_logPath;
    QString m_error;
    bool m_started = false;
};

}  // namespace nq::service
