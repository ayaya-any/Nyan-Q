#include "service/ServiceClient.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QThread>

namespace nq::service {

QString ServiceClient::cachedToken;

QString ServiceClient::readToken(QString* error)
{
    if (!cachedToken.isEmpty())
        return cachedToken;

    QFile file(tokenPath());
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 Helper 服务令牌");
        return {};
    }
    const QString token = QString::fromLatin1(file.readAll()).trimmed();
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{64}$"));
    if (!hex.match(token).hasMatch()) {
        if (error)
            *error = QStringLiteral("Helper 服务令牌无效");
        return {};
    }
    cachedToken = token;
    return token;
}

void ServiceClient::invalidateToken()
{
    cachedToken.clear();
}

QJsonObject ServiceClient::request(const QJsonObject& payload, int timeoutMs, QString* error)
{
    QString tokenError;
    const QString token = readToken(&tokenError);
    if (token.isEmpty()) {
        if (error)
            *error = tokenError;
        return {};
    }

    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kPipeName), QIODevice::ReadWrite);
    if (!socket.waitForConnected(600)) {
        if (error)
            *error = QStringLiteral("无法连接 Nyan-Q Helper 服务");
        return {};
    }

    QJsonObject object = payload;
    object.insert(QStringLiteral("token"), token);
    QByteArray wire = QJsonDocument(object).toJson(QJsonDocument::Compact);
    wire.append('\n');
    socket.write(wire);
    if (!socket.waitForBytesWritten(600)) {
        if (error)
            *error = QStringLiteral("无法向 Helper 服务发送请求");
        return {};
    }

    QByteArray response;
    QElapsedTimer timer;
    timer.start();
    while (!response.contains('\n') && timer.elapsed() < timeoutMs) {
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0)
            break;
        if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(qMin(remaining, 300)))
            break;
        response.append(socket.readAll());
    }
    socket.disconnectFromServer();

    const int newline = response.indexOf('\n');
    if (newline >= 0)
        response.truncate(newline);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(response, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("Helper 服务返回了无效响应");
        return {};
    }

    const QJsonObject result = document.object();
    if (!result.value(QStringLiteral("ok")).toBool()) {
        if (error)
            *error = result.value(QStringLiteral("error"))
                         .toString(QStringLiteral("Helper 服务拒绝了请求"));
        return {};
    }
    return result;
}

bool ServiceClient::ping(QString* error)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("command"), QStringLiteral("ping"));
    const QJsonObject response = request(payload, 1200, error);
    if (response.isEmpty() ||
        response.value(QStringLiteral("protocol")).toInt() != kProtocolVersion) {
        return false;
    }
    const QString version = response.value(QStringLiteral("version")).toString();
    if (!version.isEmpty() && version != QStringLiteral(NYANQ_VERSION)) {
        if (error)
            *error = QStringLiteral("Helper 服务版本不匹配");
        return false;
    }
    return true;
}

bool ServiceClient::isAvailable()
{
    if (!QFile::exists(tokenPath()))
        return false;
    return ping();
}

bool ServiceClient::waitUntilAvailable(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    do {
        if (isAvailable())
            return true;
        QThread::msleep(150);
    } while (timer.elapsed() < timeoutMs);
    return false;
}

bool ServiceClient::start(const QString& configPath, const QString& dataDir, int controllerPort,
                          const QString& secret, QString* error)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("command"), QStringLiteral("start"));
    payload.insert(QStringLiteral("config"), configPath);
    payload.insert(QStringLiteral("data"), dataDir);
    payload.insert(QStringLiteral("controllerPort"), controllerPort);
    payload.insert(QStringLiteral("secret"), secret);
    return !request(payload, 5000, error).isEmpty();
}

bool ServiceClient::stop(QString* error)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("command"), QStringLiteral("stop"));
    return !request(payload, 7000, error).isEmpty();
}

QJsonObject ServiceClient::status(quint64 since, QString* error)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("command"), QStringLiteral("status"));
    payload.insert(QStringLiteral("since"), static_cast<double>(since));
    return request(payload, 800, error);
}

}  // namespace nq::service
