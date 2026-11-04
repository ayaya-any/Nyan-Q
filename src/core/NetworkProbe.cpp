#include "core/NetworkProbe.h"

#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <algorithm>

namespace {
constexpr int kSiteIntervalMs = 60 * 1000;
constexpr int kExitIntervalMs = 5 * 60 * 1000;
constexpr int kRequestTimeoutMs = 7000;

QNetworkRequest makeRequest(const QUrl& url, const char* accept)
{
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Clash-Q/0.1");
    request.setRawHeader("Accept", accept);
    request.setRawHeader("Cache-Control", "no-cache");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(kRequestTimeoutMs);
    return request;
}
}  // namespace

NetworkProbe::NetworkProbe(QObject* parent)
    : QObject(parent)
{
    for (const SiteDefinition& definition : siteDefinitions()) {
        cq::SiteLatency site;
        site.id = definition.id;
        site.name = definition.name;
        site.url = definition.url.toString();
        m_sites.append(site);
    }

    m_siteTimer.setInterval(kSiteIntervalMs);
    m_exitTimer.setInterval(kExitIntervalMs);
    connect(&m_siteTimer, &QTimer::timeout, this, [this] {
        if (m_running && !m_demo)
            refreshSiteLatencies();
    });
    connect(&m_exitTimer, &QTimer::timeout, this, [this] {
        if (m_running && !m_demo)
            refreshExitInfo();
    });
}

QVector<NetworkProbe::SiteDefinition> NetworkProbe::siteDefinitions()
{
    return {
        {QStringLiteral("github"), QStringLiteral("GitHub"),
         QUrl(QStringLiteral("https://github.com/robots.txt"))},
        {QStringLiteral("google"), QStringLiteral("Google"),
         QUrl(QStringLiteral("https://www.google.com/generate_204"))},
        {QStringLiteral("youtube"), QStringLiteral("YouTube"),
         QUrl(QStringLiteral("https://www.youtube.com/generate_204"))},
    };
}

void NetworkProbe::setProxyPort(int port)
{
    if (port == m_proxyPort)
        return;
    m_proxyPort = port;
    applyProxy();
}

void NetworkProbe::applyProxy()
{
    if (m_proxyPort > 0) {
        m_network.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"),
                                         static_cast<quint16>(m_proxyPort)));
    } else {
        m_network.setProxy(QNetworkProxy::NoProxy);
    }
}

void NetworkProbe::setRunning(bool running)
{
    if (m_demo || running == m_running)
        return;

    m_running = running;
    if (running) {
        applyProxy();
        m_siteTimer.start();
        m_exitTimer.start();
        refreshAll();
        return;
    }

    m_siteTimer.stop();
    m_exitTimer.stop();
    ++m_exitGeneration;
    ++m_siteGeneration;
    cancelExitRequest();
    cancelSiteRequests();

    m_exitInfo = {};
    m_exitInfo.error = QStringLiteral("Mihomo 未连接");
    for (cq::SiteLatency& site : m_sites) {
        site.pending = false;
        site.ok = false;
        site.delayMs = -1;
        site.testedAt = {};
    }
    emit exitInfoChanged();
    emit siteLatenciesChanged();
}

void NetworkProbe::refreshAll()
{
    if (!m_running || m_demo)
        return;
    applyProxy();
    refreshExitInfo();
    refreshSiteLatencies();
}

void NetworkProbe::refreshExitInfo()
{
    if (!m_running || m_demo)
        return;
    if (m_proxyPort <= 0) {
        failExit(QStringLiteral("内核代理端口不可用"));
        return;
    }

    ++m_exitGeneration;
    cancelExitRequest();
    m_exitInfo.loading = true;
    m_exitInfo.error.clear();
    emit exitInfoChanged();
    fetchExitPrimary(m_exitGeneration);
}

void NetworkProbe::fetchExitPrimary(int generation)
{
    QNetworkReply* reply =
        m_network.get(makeRequest(QUrl(QStringLiteral("https://ipwho.is/?lang=zh-CN")),
                                  "application/json"));
    m_exitReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        if (m_exitReply == reply)
            m_exitReply = nullptr;
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        if (generation != m_exitGeneration || !m_running)
            return;

        if (status >= 200 && status < 300) {
            const QJsonDocument document = QJsonDocument::fromJson(body);
            const QJsonObject object = document.object();
            if (!object.isEmpty() && object.value(QStringLiteral("success")).toBool(true)) {
                const QString ip = object.value(QStringLiteral("ip")).toString().trimmed();
                if (!ip.isEmpty()) {
                    cq::ExitInfo info;
                    info.ip = ip;
                    info.countryCode =
                        object.value(QStringLiteral("country_code")).toString().toUpper();
                    info.countryName = localizedCountryName(
                        info.countryCode, object.value(QStringLiteral("country")).toString());
                    info.region = object.value(QStringLiteral("region")).toString();
                    info.city = object.value(QStringLiteral("city")).toString();

                    const QJsonObject connection =
                        object.value(QStringLiteral("connection")).toObject();
                    const QString isp = connection.value(QStringLiteral("isp")).toString();
                    QString asn =
                        connection.value(QStringLiteral("asn")).toVariant().toString();
                    asn = asn.trimmed();
                    if (!asn.isEmpty() && !asn.startsWith(QStringLiteral("AS"),
                                                          Qt::CaseInsensitive))
                        asn.prepend(QStringLiteral("AS"));
                    info.organization = isp;
                    if (!asn.isEmpty())
                        info.organization +=
                            (info.organization.isEmpty() ? QString() : QStringLiteral(" · ")) + asn;

                    completeExit(info);
                    return;
                }
            }
        }

        fetchExitFallback(generation);
    });
}

void NetworkProbe::fetchExitFallback(int generation)
{
    QNetworkReply* reply = m_network.get(makeRequest(
        QUrl(QStringLiteral("https://www.cloudflare.com/cdn-cgi/trace")), "text/plain"));
    m_exitReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        if (m_exitReply == reply)
            m_exitReply = nullptr;
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        if (generation != m_exitGeneration || !m_running)
            return;

        if (status >= 200 && status < 300) {
            QString ip;
            QString countryCode;
            const QList<QByteArray> lines = body.split('\n');
            for (const QByteArray& rawLine : lines) {
                const QByteArray line = rawLine.trimmed();
                const int equals = line.indexOf('=');
                if (equals <= 0)
                    continue;
                const QByteArray key = line.left(equals);
                const QByteArray value = line.mid(equals + 1);
                if (key == "ip")
                    ip = QString::fromUtf8(value).trimmed();
                else if (key == "loc")
                    countryCode = QString::fromUtf8(value).trimmed().toUpper();
            }
            if (!ip.isEmpty()) {
                cq::ExitInfo info;
                info.ip = ip;
                info.countryCode = countryCode;
                info.countryName = localizedCountryName(countryCode, {});
                info.organization = QStringLiteral("Cloudflare trace");
                completeExit(info);
                return;
            }
        }

        failExit(QStringLiteral("无法获取出口 IP，请检查节点连通性"));
    });
}

void NetworkProbe::completeExit(const cq::ExitInfo& info)
{
    m_exitInfo = info;
    m_exitInfo.loading = false;
    m_exitInfo.valid = true;
    m_exitInfo.error.clear();
    m_exitInfo.updatedAt = QDateTime::currentDateTime();
    emit exitInfoChanged();
}

void NetworkProbe::failExit(const QString& error)
{
    m_exitInfo = {};
    m_exitInfo.error = error;
    m_exitInfo.loading = false;
    m_exitInfo.valid = false;
    emit exitInfoChanged();
}

void NetworkProbe::cancelExitRequest()
{
    if (!m_exitReply)
        return;
    QNetworkReply* reply = m_exitReply;
    m_exitReply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void NetworkProbe::refreshSiteLatencies()
{
    if (!m_running || m_demo)
        return;
    if (m_proxyPort <= 0) {
        for (cq::SiteLatency& site : m_sites) {
            site.pending = false;
            site.ok = false;
            site.delayMs = -1;
        }
        emit siteLatenciesChanged();
        return;
    }

    ++m_siteGeneration;
    cancelSiteRequests();
    markSitesPending();
    for (const SiteDefinition& definition : siteDefinitions())
        requestSite(definition, m_siteGeneration);
}

void NetworkProbe::markSitesPending()
{
    for (cq::SiteLatency& site : m_sites) {
        site.pending = true;
        site.ok = false;
        site.delayMs = -1;
    }
    emit siteLatenciesChanged();
}

void NetworkProbe::requestSite(const SiteDefinition& definition, int generation)
{
    const qint64 startedAt = QDateTime::currentMSecsSinceEpoch();
    QNetworkReply* reply =
        m_network.head(makeRequest(definition.url, "text/html,application/json;q=0.9,*/*;q=0.8"));
    m_siteReplies.insert(reply);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation, id = definition.id, startedAt] {
                m_siteReplies.remove(reply);
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QNetworkReply::NetworkError error = reply->error();
                reply->deleteLater();

                if (generation != m_siteGeneration || !m_running)
                    return;

                const bool reachable = status > 0 || error == QNetworkReply::NoError;
                const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - startedAt;
                const auto it = std::find_if(m_sites.begin(), m_sites.end(),
                                             [&id](const cq::SiteLatency& site) {
                                                 return site.id == id;
                                             });
                if (it == m_sites.end())
                    return;

                it->pending = false;
                it->ok = reachable;
                it->delayMs =
                    reachable ? static_cast<int>(qBound<qint64>(qint64(1), elapsed, qint64(99999)))
                              : -1;
                it->testedAt = QDateTime::currentDateTime();
                emit siteLatenciesChanged();
            });
}

void NetworkProbe::cancelSiteRequests()
{
    const auto replies = m_siteReplies;
    m_siteReplies.clear();
    for (QNetworkReply* reply : replies) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void NetworkProbe::loadDemoData()
{
    m_demo = true;
    m_running = true;
    m_proxyPort = 7890;
    m_siteTimer.stop();
    m_exitTimer.stop();

    m_exitInfo = {};
    m_exitInfo.ip = QStringLiteral("203.0.113.42");
    m_exitInfo.countryCode = QStringLiteral("JP");
    m_exitInfo.countryName = QStringLiteral("日本");
    m_exitInfo.region = QStringLiteral("东京都");
    m_exitInfo.city = QStringLiteral("东京");
    m_exitInfo.organization = QStringLiteral("Demo Transit · AS64500");
    m_exitInfo.valid = true;
    m_exitInfo.updatedAt = QDateTime::currentDateTime();

    const QHash<QString, int> delays{
        {QStringLiteral("github"), 58},
        {QStringLiteral("google"), 43},
        {QStringLiteral("youtube"), 52},
    };
    for (cq::SiteLatency& site : m_sites) {
        site.pending = false;
        site.ok = delays.contains(site.id);
        site.delayMs = delays.value(site.id, -1);
        site.testedAt = QDateTime::currentDateTime();
    }
    emit exitInfoChanged();
    emit siteLatenciesChanged();
}

QString NetworkProbe::localizedCountryName(const QString& code, const QString& fallback)
{
    const QString normalized = code.trimmed().toUpper();
    static const QHash<QString, QString> names{
        {QStringLiteral("CN"), QStringLiteral("中国")},
        {QStringLiteral("HK"), QStringLiteral("中国香港")},
        {QStringLiteral("MO"), QStringLiteral("中国澳门")},
        {QStringLiteral("TW"), QStringLiteral("中国台湾")},
        {QStringLiteral("JP"), QStringLiteral("日本")},
        {QStringLiteral("KR"), QStringLiteral("韩国")},
        {QStringLiteral("SG"), QStringLiteral("新加坡")},
        {QStringLiteral("US"), QStringLiteral("美国")},
        {QStringLiteral("GB"), QStringLiteral("英国")},
        {QStringLiteral("CA"), QStringLiteral("加拿大")},
        {QStringLiteral("AU"), QStringLiteral("澳大利亚")},
        {QStringLiteral("NZ"), QStringLiteral("新西兰")},
        {QStringLiteral("DE"), QStringLiteral("德国")},
        {QStringLiteral("FR"), QStringLiteral("法国")},
        {QStringLiteral("NL"), QStringLiteral("荷兰")},
        {QStringLiteral("CH"), QStringLiteral("瑞士")},
        {QStringLiteral("IT"), QStringLiteral("意大利")},
        {QStringLiteral("ES"), QStringLiteral("西班牙")},
        {QStringLiteral("SE"), QStringLiteral("瑞典")},
        {QStringLiteral("NO"), QStringLiteral("挪威")},
        {QStringLiteral("DK"), QStringLiteral("丹麦")},
        {QStringLiteral("FI"), QStringLiteral("芬兰")},
        {QStringLiteral("IE"), QStringLiteral("爱尔兰")},
        {QStringLiteral("PL"), QStringLiteral("波兰")},
        {QStringLiteral("RU"), QStringLiteral("俄罗斯")},
        {QStringLiteral("UA"), QStringLiteral("乌克兰")},
        {QStringLiteral("TR"), QStringLiteral("土耳其")},
        {QStringLiteral("IN"), QStringLiteral("印度")},
        {QStringLiteral("TH"), QStringLiteral("泰国")},
        {QStringLiteral("VN"), QStringLiteral("越南")},
        {QStringLiteral("MY"), QStringLiteral("马来西亚")},
        {QStringLiteral("ID"), QStringLiteral("印度尼西亚")},
        {QStringLiteral("PH"), QStringLiteral("菲律宾")},
        {QStringLiteral("AE"), QStringLiteral("阿联酋")},
        {QStringLiteral("SA"), QStringLiteral("沙特阿拉伯")},
        {QStringLiteral("IL"), QStringLiteral("以色列")},
        {QStringLiteral("EG"), QStringLiteral("埃及")},
        {QStringLiteral("ZA"), QStringLiteral("南非")},
        {QStringLiteral("BR"), QStringLiteral("巴西")},
        {QStringLiteral("AR"), QStringLiteral("阿根廷")},
        {QStringLiteral("CL"), QStringLiteral("智利")},
        {QStringLiteral("CO"), QStringLiteral("哥伦比亚")},
        {QStringLiteral("MX"), QStringLiteral("墨西哥")},
        {QStringLiteral("PE"), QStringLiteral("秘鲁")},
    };
    if (names.contains(normalized))
        return names.value(normalized);
    if (!fallback.trimmed().isEmpty())
        return fallback.trimmed();
    return normalized;
}
