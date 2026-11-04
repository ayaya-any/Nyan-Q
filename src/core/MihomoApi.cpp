#include "core/MihomoApi.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

#include <algorithm>

namespace {
constexpr int kDelayConcurrency = 8;
}  // namespace

MihomoApi::MihomoApi(QObject* parent)
    : QObject(parent)
{
    m_connectionsTimer.setInterval(2000);
    m_proxiesTimer.setInterval(3000);
    m_rulesTimer.setInterval(15000);
    m_configTimer.setInterval(10000);

    connect(&m_connectionsTimer, &QTimer::timeout, this, &MihomoApi::fetchConnections);
    connect(&m_proxiesTimer, &QTimer::timeout, this, &MihomoApi::fetchProxies);
    connect(&m_rulesTimer, &QTimer::timeout, this, &MihomoApi::fetchRules);
    connect(&m_configTimer, &QTimer::timeout, this, &MihomoApi::fetchConfig);
    setEndpoint(9090);
}

void MihomoApi::setEndpoint(int controllerPort)
{
    m_base = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(controllerPort));
}

void MihomoApi::setSecret(const QString& secret)
{
    m_secretString = secret;
    m_secret = QStringLiteral("Bearer %1").arg(secret).toUtf8();
}

void MihomoApi::setDelayTestUrl(const QString& url)
{
    if (url.startsWith(QStringLiteral("http://")) || url.startsWith(QStringLiteral("https://")))
        m_delayTestUrl = url;
}

QUrl MihomoApi::makeUrl(const QString& path, const QUrlQuery& query) const
{
    QUrl url = m_base;
    url.setPath(path, QUrl::DecodedMode);
    if (!query.isEmpty())
        url.setQuery(query);
    return url;
}

QNetworkRequest MihomoApi::makeRequest(const QString& path, const QUrlQuery& query) const
{
    QNetworkRequest request(makeUrl(path, query));
    request.setRawHeader("Authorization", m_secret);
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(10000);
    return request;
}

QNetworkReply* MihomoApi::get(const QString& path, const QUrlQuery& query)
{
    return m_network.get(makeRequest(path, query));
}

QNetworkReply* MihomoApi::sendJson(const QByteArray& method, const QString& path,
                                   const QJsonObject& body, const QString& description)
{
    QNetworkRequest request = makeRequest(path);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json; charset=utf-8"));
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply* reply = m_network.sendCustomRequest(request, method, payload);
    connect(reply, &QNetworkReply::finished, this, [this, reply, description] {
        const QByteArray bodyBytes = reply->readAll();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString error = replyError(reply, bodyBytes);
        if (!error.isEmpty() || (status >= 400)) {
            emit actionFailed(description, error.isEmpty() ? QStringLiteral("HTTP %1").arg(status)
                                                           : error);
        } else {
            emit actionSucceeded(description);
        }
        reply->deleteLater();
    });
    return reply;
}

QString MihomoApi::replyError(QNetworkReply* reply, const QByteArray& body)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QString message = doc.object().value(QStringLiteral("message")).toString();
        if (!message.isEmpty())
            return message;
    }
    if (reply->error() == QNetworkReply::OperationCanceledError)
        return {};
    if (status >= 400)
        return QStringLiteral("HTTP %1").arg(status);
    if (reply->error() != QNetworkReply::NoError)
        return reply->errorString();
    return {};
}

void MihomoApi::startPolling()
{
    if (m_polling)
        return;
    m_polling = true;
    fetchConfig();
    fetchProxies();
    fetchConnections();
    fetchRules();
    startTrafficStream();
    m_connectionsTimer.start();
    m_proxiesTimer.start();
    m_rulesTimer.start();
    m_configTimer.start();
}

void MihomoApi::stopPolling()
{
    m_polling = false;
    m_connectionsTimer.stop();
    m_proxiesTimer.stop();
    m_rulesTimer.stop();
    m_configTimer.stop();
    stopTrafficStream();
    cancelReload();
    m_delayQueue.clear();
    m_delayQueued.clear();
    m_delayActive = 0;
}

void MihomoApi::probe()
{
    QNetworkReply* reply = get(QStringLiteral("/version"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray body = reply->readAll();
        const QString error = replyError(reply, body);
        if (!error.isEmpty()) {
            emit probeFailed(error);
            reply->deleteLater();
            return;
        }
        const QJsonObject object = QJsonDocument::fromJson(body).object();
        if (object.value(QStringLiteral("version")).toString().isEmpty()) {
            emit probeFailed(QStringLiteral("外部控制器返回了无效响应"));
        } else {
            emit probeSucceeded(object.value(QStringLiteral("version")).toString(),
                                object.value(QStringLiteral("meta")).toBool());
        }
        reply->deleteLater();
    });
}

void MihomoApi::fetchConfig()
{
    QNetworkReply* reply = get(QStringLiteral("/configs"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray body = reply->readAll();
        const QString error = replyError(reply, body);
        if (!error.isEmpty()) {
            if (m_polling)
                emit requestFailed(QStringLiteral("读取配置"), error);
            reply->deleteLater();
            return;
        }
        emit configReceived(QJsonDocument::fromJson(body).object());
        reply->deleteLater();
    });
}

void MihomoApi::fetchProxies()
{
    QNetworkReply* reply = get(QStringLiteral("/proxies"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray body = reply->readAll();
        const QString error = replyError(reply, body);
        if (!error.isEmpty()) {
            if (m_polling)
                emit requestFailed(QStringLiteral("读取代理"), error);
            reply->deleteLater();
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(body).object();
        emit proxiesReceived(parseProxyGroups(root.value(QStringLiteral("proxies")).toObject()));
        reply->deleteLater();
    });
}

void MihomoApi::fetchConnections()
{
    QNetworkReply* reply = get(QStringLiteral("/connections"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray body = reply->readAll();
        const QString error = replyError(reply, body);
        if (!error.isEmpty()) {
            if (m_polling)
                emit requestFailed(QStringLiteral("读取连接"), error);
            reply->deleteLater();
            return;
        }
        qint64 up = 0;
        qint64 down = 0;
        const auto connections =
            parseConnections(QJsonDocument::fromJson(body).object(), &up, &down);
        emit connectionsReceived(connections, up, down);
        reply->deleteLater();
    });
}

void MihomoApi::fetchRules()
{
    QNetworkReply* reply = get(QStringLiteral("/rules"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray body = reply->readAll();
        const QString error = replyError(reply, body);
        if (!error.isEmpty()) {
            if (m_polling)
                emit requestFailed(QStringLiteral("读取规则"), error);
            reply->deleteLater();
            return;
        }
        QVector<cq::RuleInfo> rules;
        const QJsonArray list =
            QJsonDocument::fromJson(body).object().value(QStringLiteral("rules")).toArray();
        rules.reserve(list.size());
        for (const QJsonValue& value : list) {
            const QJsonObject object = value.toObject();
            cq::RuleInfo rule;
            rule.type = object.value(QStringLiteral("type")).toString();
            rule.payload = object.value(QStringLiteral("payload")).toString();
            rule.proxy = object.value(QStringLiteral("proxy")).toString();
            if (!rule.type.isEmpty())
                rules.append(rule);
        }
        emit rulesReceived(rules);
        reply->deleteLater();
    });
}

void MihomoApi::startTrafficStream()
{
    stopTrafficStream();
    QNetworkRequest request = makeRequest(QStringLiteral("/traffic"));
    request.setTransferTimeout(0);
    request.setRawHeader("Accept", "application/json");
    m_trafficReply = m_network.get(request);
    m_trafficBuffer.clear();

    connect(m_trafficReply, &QNetworkReply::readyRead, this, &MihomoApi::consumeTraffic);
    connect(m_trafficReply, &QNetworkReply::finished, this, [this] {
        if (!m_trafficReply)
            return;
        QNetworkReply* reply = m_trafficReply;
        m_trafficReply = nullptr;
        const bool shouldReconnect =
            m_polling && reply->error() != QNetworkReply::OperationCanceledError;
        reply->deleteLater();
        if (shouldReconnect) {
            QTimer::singleShot(1200, this, [this] {
                if (m_polling && !m_trafficReply)
                    startTrafficStream();
            });
        }
    });
}

void MihomoApi::stopTrafficStream()
{
    if (!m_trafficReply)
        return;
    QNetworkReply* reply = m_trafficReply;
    m_trafficReply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
    m_trafficBuffer.clear();
}

void MihomoApi::consumeTraffic()
{
    if (!m_trafficReply)
        return;
    m_trafficBuffer += m_trafficReply->readAll();
    auto parseLine = [this](const QByteArray& line) {
        if (line.trimmed().isEmpty())
            return;
        const QJsonObject object = QJsonDocument::fromJson(line).object();
        if (!object.contains(QStringLiteral("up")) && !object.contains(QStringLiteral("down")))
            return;
        emit trafficReceived(static_cast<qint64>(object.value(QStringLiteral("up")).toDouble()),
                             static_cast<qint64>(object.value(QStringLiteral("down")).toDouble()));
    };

    int newline = m_trafficBuffer.indexOf('\n');
    while (newline >= 0) {
        parseLine(m_trafficBuffer.left(newline));
        m_trafficBuffer.remove(0, newline + 1);
        newline = m_trafficBuffer.indexOf('\n');
    }
    if (!m_trafficBuffer.isEmpty() && m_trafficBuffer.endsWith('}')) {
        const QJsonDocument doc = QJsonDocument::fromJson(m_trafficBuffer);
        if (!doc.isNull()) {
            parseLine(m_trafficBuffer);
            m_trafficBuffer.clear();
        }
    }
}

void MihomoApi::cancelReload()
{
    if (!m_reloadReply)
        return;
    QNetworkReply* reply = m_reloadReply;
    m_reloadReply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void MihomoApi::reloadConfig(const QString& absolutePath, bool force)
{
    cancelReload();

    QUrlQuery query;
    if (force)
        query.addQueryItem(QStringLiteral("force"), QStringLiteral("true"));

    QNetworkRequest request = makeRequest(QStringLiteral("/configs"), query);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json; charset=utf-8"));
    request.setTransferTimeout(30000);

    QJsonObject body;
    body.insert(QStringLiteral("path"), absolutePath);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = m_network.sendCustomRequest(request, "PUT", payload);
    m_reloadReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_reloadReply != reply) {
            reply->deleteLater();
            return;
        }
        m_reloadReply = nullptr;
        const QByteArray bodyBytes = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error = replyError(reply, bodyBytes);
        if (error.isEmpty() && status >= 400)
            error = QStringLiteral("HTTP %1").arg(status);
        if (error.isEmpty() && reply->error() != QNetworkReply::NoError)
            error = reply->errorString();
        emit configReloaded(error.isEmpty(), error);
        reply->deleteLater();
    });
}

void MihomoApi::setMode(const QString& mode)
{
    QJsonObject body;
    body.insert(QStringLiteral("mode"), mode);
    const QString display = mode == QStringLiteral("global") ? QStringLiteral("全局")
                            : mode == QStringLiteral("direct") ? QStringLiteral("直连")
                                                               : QStringLiteral("规则");
    sendJson("PATCH", QStringLiteral("/configs"), body,
             QStringLiteral("切换为%1模式").arg(display));
}

void MihomoApi::selectProxy(const QString& group, const QString& proxy)
{
    QJsonObject body;
    body.insert(QStringLiteral("name"), proxy);
    sendJson("PUT", QStringLiteral("/proxies/") + group, body,
             QStringLiteral("切换节点到 %1").arg(proxy));
}

void MihomoApi::testDelay(const QString& proxyName)
{
    if (proxyName.isEmpty() || m_delayQueued.contains(proxyName))
        return;
    m_delayQueue.enqueue(proxyName);
    m_delayQueued.insert(proxyName);
    pumpDelayQueue();
}

void MihomoApi::pumpDelayQueue()
{
    while (m_delayActive < kDelayConcurrency && !m_delayQueue.isEmpty()) {
        const QString name = m_delayQueue.dequeue();
        if (name.isEmpty()) {
            m_delayQueued.remove(name);
            continue;
        }
        ++m_delayActive;
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("timeout"), QStringLiteral("5000"));
        query.addQueryItem(QStringLiteral("url"), m_delayTestUrl);
        QNetworkReply* reply =
            get(QStringLiteral("/proxies/") + name + QStringLiteral("/delay"), query);
        connect(reply, &QNetworkReply::finished, this, [this, reply, name] {
            const QByteArray body = reply->readAll();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            int delay = -1;
            if (reply->error() == QNetworkReply::NoError && status < 400) {
                const QJsonObject object = QJsonDocument::fromJson(body).object();
                if (object.contains(QStringLiteral("delay")))
                    delay = object.value(QStringLiteral("delay")).toInt(-1);
            } else if (body.contains("timeout") || reply->error() == QNetworkReply::TimeoutError) {
                delay = 0;
            }
            --m_delayActive;
            finishDelay(name, delay);
            reply->deleteLater();
            QTimer::singleShot(20, this, &MihomoApi::pumpDelayQueue);
        });
    }
}

void MihomoApi::finishDelay(const QString& name, int delay)
{
    m_delayQueued.remove(name);
    emit delayTested(name, delay);
}

void MihomoApi::closeConnection(const QString& id)
{
    sendJson("DELETE", QStringLiteral("/connections/") + id, {},
             QStringLiteral("关闭连接"));
}

void MihomoApi::closeAllConnections()
{
    sendJson("DELETE", QStringLiteral("/connections"), {},
             QStringLiteral("关闭全部连接"));
}

int MihomoApi::latestDelay(const QJsonObject& proxy)
{
    const QJsonArray history = proxy.value(QStringLiteral("history")).toArray();
    if (history.isEmpty())
        return -1;
    const QJsonObject latest = history.last().toObject();
    if (!latest.contains(QStringLiteral("delay")))
        return -1;
    return latest.value(QStringLiteral("delay")).toInt(-1);
}

QVector<cq::ProxyGroup> MihomoApi::parseProxyGroups(const QJsonObject& proxiesRoot)
{
    QVector<cq::ProxyGroup> groups;
    for (auto it = proxiesRoot.constBegin(); it != proxiesRoot.constEnd(); ++it) {
        const QJsonObject object = it.value().toObject();
        const QJsonArray all = object.value(QStringLiteral("all")).toArray();
        if (all.isEmpty())
            continue;
        if (it.key() == QStringLiteral("GLOBAL"))
            continue;
        if (object.value(QStringLiteral("hidden")).toBool())
            continue;

        cq::ProxyGroup group;
        group.name = it.key();
        group.type = object.value(QStringLiteral("type")).toString();
        group.now = object.value(QStringLiteral("now")).toString();
        const int groupDelay = latestDelay(object);

        for (const QJsonValue& nodeValue : all) {
            const QString nodeName = nodeValue.toString();
            if (nodeName.isEmpty())
                continue;
            cq::ProxyNode node;
            node.name = nodeName;
            if (proxiesRoot.contains(nodeName)) {
                const QJsonObject nodeObject = proxiesRoot.value(nodeName).toObject();
                node.type = nodeObject.value(QStringLiteral("type")).toString();
                node.udp = nodeObject.value(QStringLiteral("udp")).toBool();
                node.alive = nodeObject.value(QStringLiteral("alive")).toBool(true);
                node.delay = latestDelay(nodeObject);
            } else {
                node.delay = groupDelay;
            }
            if (group.now.isEmpty() && group.type.contains(QStringLiteral("URLTest"), Qt::CaseInsensitive))
                group.now = node.name;
            group.nodes.append(node);
        }
        if (group.name.isEmpty())
            continue;
        groups.append(group);
    }

    auto rank = [](const QString& type) {
        const QString lower = type.toLower();
        if (lower.contains(QStringLiteral("selector")))
            return 0;
        if (lower.contains(QStringLiteral("urltest")))
            return 1;
        if (lower.contains(QStringLiteral("fallback")))
            return 2;
        if (lower.contains(QStringLiteral("loadbalance")))
            return 3;
        if (lower.contains(QStringLiteral("relay")))
            return 4;
        return 5;
    };
    std::sort(groups.begin(), groups.end(), [&rank](const cq::ProxyGroup& a, const cq::ProxyGroup& b) {
        const int ra = rank(a.type);
        const int rb = rank(b.type);
        if (ra != rb)
            return ra < rb;
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return groups;
}

QVector<cq::ConnectionInfo> MihomoApi::parseConnections(const QJsonObject& root,
                                                         qint64* uploadTotal,
                                                         qint64* downloadTotal)
{
    if (uploadTotal)
        *uploadTotal = static_cast<qint64>(root.value(QStringLiteral("uploadTotal")).toDouble());
    if (downloadTotal)
        *downloadTotal = static_cast<qint64>(root.value(QStringLiteral("downloadTotal")).toDouble());

    QVector<cq::ConnectionInfo> result;
    const QJsonArray connections = root.value(QStringLiteral("connections")).toArray();
    result.reserve(connections.size());
    for (const QJsonValue& value : connections) {
        const QJsonObject object = value.toObject();
        const QJsonObject metadata = object.value(QStringLiteral("metadata")).toObject();
        cq::ConnectionInfo info;
        info.id = object.value(QStringLiteral("id")).toString();
        info.host = metadata.value(QStringLiteral("host")).toString();
        if (info.host.isEmpty())
            info.host = metadata.value(QStringLiteral("destinationIP")).toString();
        const QString port = metadata.value(QStringLiteral("destinationPort")).toString();
        if (!port.isEmpty() && info.host != port)
            info.host += QStringLiteral(":") + port;
        info.network = metadata.value(QStringLiteral("network")).toString();
        info.type = metadata.value(QStringLiteral("type")).toString();
        info.rule = object.value(QStringLiteral("rule")).toString();
        info.process = metadata.value(QStringLiteral("process")).toString();
        info.upload = static_cast<qint64>(object.value(QStringLiteral("upload")).toDouble());
        info.download = static_cast<qint64>(object.value(QStringLiteral("download")).toDouble());
        info.startedAt =
            QDateTime::fromString(object.value(QStringLiteral("start")).toString(), Qt::ISODate);
        const QJsonArray chains = object.value(QStringLiteral("chains")).toArray();
        QStringList chainList;
        for (const QJsonValue& chain : chains)
            chainList << chain.toString();
        info.chain = chainList.join(QStringLiteral(" → "));
        if (info.host.isEmpty())
            info.host = QStringLiteral("未知目标");
        result.append(info);
    }
    return result;
}
