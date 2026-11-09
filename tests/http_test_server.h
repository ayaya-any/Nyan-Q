#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QUrl>

class QTcpSocket;

// Minimal loopback HTTP server for downloader/manager tests. It deliberately
// supports only one request per connection and a tiny subset of HTTP.
class TestHttpServer : public QObject {
    Q_OBJECT

public:
    struct Route {
        int status = 200;
        QByteArray body;
        QByteArray contentType = "application/javascript; charset=utf-8";
        QByteArray etag;
        QByteArray lastModified;
        int delayMs = 0;
        bool hang = false;
    };

    explicit TestHttpServer(QObject* parent = nullptr);

    bool start();
    void stop();
    QString baseUrl() const;
    QUrl url(const QString& path) const;

    void setRoute(const QString& path, const Route& route);
    int requestCount(const QString& path) const;
    QHash<QByteArray, QByteArray> lastRequestHeaders(const QString& path) const;

private:
    void handleConnection(QTcpSocket* socket);

    QTcpServer m_server;
    QHash<QString, Route> m_routes;
    QHash<QString, int> m_counts;
    QHash<QString, QHash<QByteArray, QByteArray>> m_headers;
};
