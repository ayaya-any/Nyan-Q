#include "service/ServiceServer.h"

#include "service/ServiceProtocol.h"
#include "service/ServiceRuntime.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>

#include <utility>

namespace nq::service {

ServiceServer::ServiceServer(QString token, ServiceRuntime* runtime, QObject* parent)
    : QObject(parent)
    , m_token(std::move(token))
    , m_runtime(runtime)
{
    connect(&m_server, &QLocalServer::newConnection, this, &ServiceServer::acceptConnection);
}

bool ServiceServer::start(QString* error)
{
    if (m_token.size() != 64) {
        if (error)
            *error = QStringLiteral("Helper 服务令牌无效");
        return false;
    }
    QLocalServer::removeServer(QString::fromLatin1(kPipeName));
    m_server.setSocketOptions(QLocalServer::WorldAccessOption);
    if (!m_server.listen(QString::fromLatin1(kPipeName))) {
        if (error)
            *error = m_server.errorString();
        return false;
    }
    return true;
}

void ServiceServer::stop()
{
    m_server.close();
    QLocalServer::removeServer(QString::fromLatin1(kPipeName));
}

void ServiceServer::acceptConnection()
{
    while (m_server.hasPendingConnections()) {
        QLocalSocket* socket = m_server.nextPendingConnection();
        if (!socket)
            continue;
        m_buffers.insert(socket, {});
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readClient(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
        if (socket->bytesAvailable() > 0)
            readClient(socket);
    }
}

void ServiceServer::readClient(QLocalSocket* socket)
{
    QByteArray& buffer = m_buffers[socket];
    buffer.append(socket->readAll());
    const int newline = buffer.indexOf('\n');
    if (newline < 0)
        return;

    const QByteArray request = buffer.left(newline);
    m_buffers.remove(socket);
    bool authorized = false;
    const QByteArray response = handle(request, &authorized);
    socket->write(response + '\n');
    socket->flush();
    socket->disconnectFromServer();
    Q_UNUSED(authorized)
}

QByteArray ServiceServer::handle(const QByteArray& request, bool* authorized)
{
    QJsonObject response;
    response.insert(QStringLiteral("protocol"), kProtocolVersion);
    response.insert(QStringLiteral("ok"), false);

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(request, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        response.insert(QStringLiteral("error"), QStringLiteral("无效请求"));
        return QJsonDocument(response).toJson(QJsonDocument::Compact);
    }

    const QJsonObject object = document.object();
    const QByteArray token = object.value(QStringLiteral("token")).toString().toLatin1();
    *authorized = tokenMatches(token, m_token.toLatin1());
    if (!*authorized) {
        response.insert(QStringLiteral("error"), QStringLiteral("未授权"));
        return QJsonDocument(response).toJson(QJsonDocument::Compact);
    }

    const QString command = object.value(QStringLiteral("command")).toString();
    if (command == QStringLiteral("ping")) {
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("protocol"), kProtocolVersion);
        response.insert(QStringLiteral("version"), QStringLiteral(NYANQ_VERSION));
        return QJsonDocument(response).toJson(QJsonDocument::Compact);
    }
    if (command == QStringLiteral("start")) {
        QString error;
        const bool started = m_runtime->start(
            object.value(QStringLiteral("config")).toString(),
            object.value(QStringLiteral("data")).toString(),
            object.value(QStringLiteral("controllerPort")).toInt(),
            object.value(QStringLiteral("secret")).toString(), &error);
        response.insert(QStringLiteral("ok"), started);
        if (!started)
            response.insert(QStringLiteral("error"), error);
        return QJsonDocument(response).toJson(QJsonDocument::Compact);
    }
    if (command == QStringLiteral("stop")) {
        QString error;
        const bool stopped = m_runtime->stop(&error);
        response.insert(QStringLiteral("ok"), stopped);
        if (!stopped)
            response.insert(QStringLiteral("error"), error);
        return QJsonDocument(response).toJson(QJsonDocument::Compact);
    }
    if (command == QStringLiteral("status")) {
        const quint64 since = static_cast<quint64>(
            object.value(QStringLiteral("since")).toDouble());
        const QJsonObject status = m_runtime->status(since);
        for (auto it = status.begin(); it != status.end(); ++it)
            response.insert(it.key(), it.value());
        response.insert(QStringLiteral("ok"), true);
        return QJsonDocument(response).toJson(QJsonDocument::Compact);
    }

    response.insert(QStringLiteral("error"), QStringLiteral("未知命令"));
    return QJsonDocument(response).toJson(QJsonDocument::Compact);
}

bool ServiceServer::tokenMatches(const QByteArray& a, const QByteArray& b)
{
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a.at(i) ^ b.at(i));
    return diff == 0;
}

}  // namespace nq::service
