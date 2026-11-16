#pragma once

#include "service/ServiceProtocol.h"

#include <QJsonObject>
#include <QString>

namespace nq::service {

class ServiceClient final {
public:
    static bool isAvailable();
    static bool waitUntilAvailable(int timeoutMs = 3000);
    static bool ping(QString* error = nullptr);
    static bool start(const QString& configPath, const QString& dataDir, int controllerPort,
                      const QString& secret, QString* error = nullptr);
    static bool stop(QString* error = nullptr);
    static QJsonObject status(quint64 since, QString* error = nullptr);
    static QString readToken(QString* error = nullptr);
    static void invalidateToken();

private:
    static QJsonObject request(const QJsonObject& payload, int timeoutMs, QString* error);
    static QString cachedToken;
};

}  // namespace nq::service
