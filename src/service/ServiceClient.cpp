#include "service/ServiceClient.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QStringList>
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
            *error = QStringLiteral("无法读取 Helper 服务令牌：%1").arg(file.errorString());
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
    if (error)
        error->clear();
    const QString command = payload.value(QStringLiteral("command")).toString();
    // A lost response does not mean that a command was not executed. Only
    // read-only operations may be replayed automatically on a new connection.
    const int attempts = command == QStringLiteral("ping") || command == QStringLiteral("status")
                             ? 2 : 1;
    QStringList failures;
    QElapsedTimer elapsed;
    elapsed.start();
    auto remainingBudget = [&] { return qMax(0, timeoutMs - static_cast<int>(elapsed.elapsed())); };
    for (int attempt = 1; attempt <= attempts; ++attempt) {
        QLocalSocket socket;
        auto diagnostic = [&](const QString& message, const QString& stage, int deadline,
                              qint64 received = 0) {
            return QStringLiteral("%1 [command=%2 stage=%3 attempt=%4/%5 socketError=%6 "
                                  "state=%7 elapsedMs=%8 timeoutMs=%9 pendingBytes=%10 "
                                  "receivedBytes=%11 detail=%12]")
                .arg(message, command, stage).arg(attempt).arg(attempts)
                .arg(static_cast<int>(socket.error())).arg(static_cast<int>(socket.state()))
                .arg(elapsed.elapsed()).arg(deadline).arg(socket.bytesToWrite())
                .arg(received).arg(socket.errorString());
        };
        bool retryable = true;
        const QJsonObject result = [&]() -> QJsonObject {
            QString tokenError;
            const QString token = readToken(&tokenError);
            if (token.isEmpty()) {
                failures.append(diagnostic(tokenError, QStringLiteral("token"), 0));
                retryable = false;
                return {};
            }
            socket.connectToServer(QString::fromLatin1(kPipeName), QIODevice::ReadWrite);
            if (!socket.waitForConnected(qMin(600, remainingBudget()))) {
                failures.append(diagnostic(QStringLiteral("无法连接 Nyan-Q Helper 服务"),
                                           QStringLiteral("connect"), 600));
                return {};
            }
            QJsonObject object = payload;
            object.insert(QStringLiteral("token"), token);
            QByteArray wire = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
            const qint64 accepted = socket.write(wire);
            QElapsedTimer writeTimer;
            writeTimer.start();
            while (accepted == wire.size() && socket.bytesToWrite() > 0) {
                const int remaining = qMin(600 - static_cast<int>(writeTimer.elapsed()), remainingBudget());
                if (remaining <= 0 || !socket.waitForBytesWritten(remaining))
                    break;
            }
            // waitForBytesWritten() is an event wait, not a delivery receipt.
            // An already-drained write must not be classified as a failure.
            if (accepted != wire.size() || socket.bytesToWrite() > 0) {
                failures.append(diagnostic(QStringLiteral("无法向 Helper 服务发送请求"),
                                           QStringLiteral("write"), 600));
                return {};
            }
            QByteArray response;
            while (!response.contains('\n')) {
                response.append(socket.readAll());
                if (response.contains('\n'))
                    break;
                const int remaining = remainingBudget();
                if (remaining <= 0 || socket.state() == QLocalSocket::UnconnectedState)
                    break;
                // A 300 ms read slice timing out is not the request deadline.
                socket.waitForReadyRead(qMin(remaining, 300));
            }
            const int newline = response.indexOf('\n');
            if (newline < 0) {
                failures.append(diagnostic(QStringLiteral("等待 Helper 响应失败（超时或连接中断）"),
                                           QStringLiteral("read"), timeoutMs, response.size()));
                return {};
            }
            response.truncate(newline);
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(response, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                failures.append(diagnostic(
                    QStringLiteral("Helper 服务返回了无效响应（JSON error=%1 offset=%2）")
                        .arg(static_cast<int>(parseError.error)).arg(parseError.offset),
                    QStringLiteral("parse"), timeoutMs, response.size()));
                retryable = false;
                return {};
            }
            const QJsonObject reply = document.object();
            if (!reply.value(QStringLiteral("ok")).toBool()) {
                const QString reason = reply.value(QStringLiteral("error"))
                    .toString(QStringLiteral("Helper 服务拒绝了请求"));
                failures.append(diagnostic(reason, QStringLiteral("response"), timeoutMs,
                                           response.size()));
                retryable = reason == QStringLiteral("未授权");
                return {};
            }
            return reply;
        }();
        if (!result.isEmpty())
            return result;
        if (!retryable || remainingBudget() <= 0)
            break;
        // A Helper update can replace its token while this GUI is still open.
        invalidateToken();
    }
    if (error)
        *error = failures.join(QStringLiteral("；重连后："));
    return {};
}

bool ServiceClient::ping(QString* error)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("command"), QStringLiteral("ping"));
    const QJsonObject response = request(payload, 1200, error);
    if (response.isEmpty())
        return false;
    if (response.value(QStringLiteral("protocol")).toInt() != kProtocolVersion) {
        if (error)
            *error = QStringLiteral("Helper 服务协议不匹配（expected=%1 actual=%2）")
                .arg(kProtocolVersion).arg(response.value(QStringLiteral("protocol")).toInt());
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
