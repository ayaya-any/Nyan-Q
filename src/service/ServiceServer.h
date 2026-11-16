#pragma once

#include <QHash>
#include <QLocalServer>
#include <QObject>
#include <QString>

class QLocalSocket;

namespace nq::service {

class ServiceRuntime;

class ServiceServer final : public QObject {
    Q_OBJECT

public:
    ServiceServer(QString token, ServiceRuntime* runtime, QObject* parent = nullptr);
    bool start(QString* error);
    void stop();

private:
    void acceptConnection();
    void readClient(QLocalSocket* socket);
    QByteArray handle(const QByteArray& request, bool* authorized);
    static bool tokenMatches(const QByteArray& a, const QByteArray& b);

    QString m_token;
    ServiceRuntime* m_runtime = nullptr;
    QLocalServer m_server;
    QHash<QLocalSocket*, QByteArray> m_buffers;
};

}  // namespace nq::service
