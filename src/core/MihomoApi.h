#pragma once

#include "core/Types.h"

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

class QNetworkReply;
class QNetworkRequest;

class MihomoApi final : public QObject {
    Q_OBJECT

public:
    explicit MihomoApi(QObject* parent = nullptr);

    void setEndpoint(int controllerPort);
    void setSecret(const QString& secret);
    void setDelayTestUrl(const QString& url);
    QString delayTestUrl() const { return m_delayTestUrl; }
    QUrl baseUrl() const { return m_base; }

    bool isPolling() const { return m_polling; }
    void startPolling();
    void stopPolling();

    void probe();
    void fetchConfig();
    void fetchProxies();
    void fetchConnections();
    void fetchRules();

    // Uses Mihomo's PUT /configs?force=true endpoint to hot reload the full
    // configuration without terminating the kernel process.
    void reloadConfig(const QString& absolutePath, bool force = true);
    void setMode(const QString& mode);
    void selectProxy(const QString& group, const QString& proxy);
    void testDelay(const QString& proxyName);
    void closeConnection(const QString& id);
    void closeAllConnections();

    static QVector<cq::ProxyGroup> parseProxyGroups(const QJsonObject& proxiesRoot);
    static QVector<cq::ConnectionInfo> parseConnections(const QJsonObject& root,
                                                        qint64* uploadTotal,
                                                        qint64* downloadTotal);

signals:
    void probeSucceeded(const QString& version, bool meta);
    void probeFailed(const QString& message);
    void configReceived(const QJsonObject& config);
    void configReloaded(bool success, const QString& error);
    void proxiesReceived(const QVector<cq::ProxyGroup>& groups);
    void connectionsReceived(const QVector<cq::ConnectionInfo>& connections, qint64 uploadTotal,
                             qint64 downloadTotal);
    void rulesReceived(const QVector<cq::RuleInfo>& rules);
    void trafficReceived(qint64 uploadSpeed, qint64 downloadSpeed);
    void delayTested(const QString& proxyName, int delayMs);
    void actionSucceeded(const QString& description);
    void actionFailed(const QString& description, const QString& message);
    void requestFailed(const QString& context, const QString& message);

private:
    QNetworkRequest makeRequest(const QString& path, const QUrlQuery& query = {}) const;
    QUrl makeUrl(const QString& path, const QUrlQuery& query = {}) const;
    QNetworkReply* get(const QString& path, const QUrlQuery& query = {});
    QNetworkReply* sendJson(const QByteArray& method, const QString& path, const QJsonObject& body,
                            const QString& description);
    void startTrafficStream();
    void stopTrafficStream();
    void cancelReload();
    void consumeTraffic();
    void pumpDelayQueue();
    void finishDelay(const QString& name, int delay);
    static QString replyError(QNetworkReply* reply, const QByteArray& body);
    static int latestDelay(const QJsonObject& proxy);

    QNetworkAccessManager m_network;
    QUrl m_base;
    QByteArray m_secret;
    QTimer m_connectionsTimer;
    QTimer m_proxiesTimer;
    QTimer m_rulesTimer;
    QTimer m_configTimer;
    QNetworkReply* m_trafficReply = nullptr;
    QNetworkReply* m_reloadReply = nullptr;
    QByteArray m_trafficBuffer;
    QQueue<QString> m_delayQueue;
    QSet<QString> m_delayQueued;
    int m_delayActive = 0;
    QString m_delayTestUrl = QStringLiteral("http://www.gstatic.com/generate_204");
    QString m_secretString;
    bool m_polling = false;
};
