#include "http_test_server.h"

#include <QHostAddress>
#include <QTcpSocket>
#include <QTimer>

TestHttpServer::TestHttpServer(QObject* parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket* socket = m_server.nextPendingConnection())
            handleConnection(socket);
    });
}

bool TestHttpServer::start()
{
    return m_server.listen(QHostAddress::LocalHost, 0);
}

void TestHttpServer::stop()
{
    m_server.close();
}

QString TestHttpServer::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
}

QUrl TestHttpServer::url(const QString& path) const
{
    return QUrl(baseUrl() + path);
}

void TestHttpServer::setRoute(const QString& path, const Route& route)
{
    m_routes.insert(path, route);
}

int TestHttpServer::requestCount(const QString& path) const
{
    return m_counts.value(path);
}

QHash<QByteArray, QByteArray> TestHttpServer::lastRequestHeaders(const QString& path) const
{
    return m_headers.value(path);
}

void TestHttpServer::handleConnection(QTcpSocket* socket)
{
    auto* buffer = new QByteArray;
    connect(socket, &QTcpSocket::destroyed, socket, [buffer] { delete buffer; });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
        buffer->append(socket->readAll());
        if (!buffer->contains("\r\n\r\n"))
            return;
        const QByteArray headerBlock = buffer->left(buffer->indexOf("\r\n\r\n"));
        buffer->clear();

        const QList<QByteArray> lines = headerBlock.split('\n');
        if (lines.isEmpty())
            return;
        const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
        if (requestLine.size() < 2)
            return;
        QString path = QString::fromUtf8(requestLine.at(1));
        const int query = path.indexOf(QLatin1Char('?'));
        if (query >= 0)
            path = path.left(query);

        QHash<QByteArray, QByteArray> headers;
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const int colon = line.indexOf(':');
            if (colon <= 0)
                continue;
            headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
        }
        m_counts[path] += 1;
        m_headers[path] = headers;

        const Route route = m_routes.value(path);
        auto respond = [socket, route] {
            if (socket->state() != QAbstractSocket::ConnectedState)
                return;
            int status = route.status;
            QByteArray body = route.body;
            if (status == 200 && !route.etag.isEmpty()) {
                // Route handlers may emulate a conditional GET by using a
                // dedicated path; this keeps the test server intentionally
                // simple.
            }
            QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " OK\r\n";
            if (status == 304)
                response = "HTTP/1.1 304 Not Modified\r\n";
            response += "Content-Type: " + route.contentType + "\r\n";
            response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
            if (!route.etag.isEmpty())
                response += "ETag: " + route.etag + "\r\n";
            if (!route.lastModified.isEmpty())
                response += "Last-Modified: " + route.lastModified + "\r\n";
            response += "Connection: close\r\n\r\n";
            response += body;
            socket->write(response);
            socket->disconnectFromHost();
        };

        if (route.hang)
            return;
        if (route.delayMs > 0)
            QTimer::singleShot(route.delayMs, socket, respond);
        else
            respond();
    });
}
