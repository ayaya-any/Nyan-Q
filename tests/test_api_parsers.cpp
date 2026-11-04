#include "core/MihomoApi.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

class ApiParsersTest : public QObject {
    Q_OBJECT

private slots:
    void parsesProxyGroups();
    void skipsGlobalAndHiddenGroups();
    void parsesConnections();
};

void ApiParsersTest::parsesProxyGroups()
{
    const QByteArray json = R"({
        "proxies": {
            "DIRECT": {"type": "Direct", "udp": true, "alive": true, "history": []},
            "香港 01": {"type": "Shadowsocks", "udp": true, "alive": true,
                       "history": [{"time": "2026-01-01T00:00:00Z", "delay": 42}]},
            "日本 01": {"type": "Trojan", "udp": false, "alive": false,
                       "history": [{"time": "2026-01-01T00:00:00Z", "delay": 180}]},
            "节点选择": {"type": "Selector", "now": "香港 01",
                        "all": ["香港 01", "日本 01", "DIRECT"], "history": []}
        }
    })";
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QVector<cq::ProxyGroup> groups =
        MihomoApi::parseProxyGroups(root.value(QStringLiteral("proxies")).toObject());

    QCOMPARE(groups.size(), 1);
    const cq::ProxyGroup& group = groups.first();
    QCOMPARE(group.name, QStringLiteral("节点选择"));
    QCOMPARE(group.type, QStringLiteral("Selector"));
    QCOMPARE(group.now, QStringLiteral("香港 01"));
    QCOMPARE(group.nodes.size(), 3);
    QCOMPARE(group.nodes.at(0).delay, 42);
    QCOMPARE(group.nodes.at(1).delay, 180);
    QVERIFY(!group.nodes.at(1).alive);
    QCOMPARE(group.nodes.at(2).name, QStringLiteral("DIRECT"));
    QCOMPARE(group.nodes.at(2).type, QStringLiteral("Direct"));
}

void ApiParsersTest::skipsGlobalAndHiddenGroups()
{
    const QByteArray json = R"({
        "proxies": {
            "DIRECT": {"type": "Direct"},
            "GLOBAL": {"type": "Selector", "all": ["DIRECT"]},
            "Hidden Group": {"type": "Selector", "hidden": true, "all": ["DIRECT"]},
            "Visible": {"type": "URLTest", "all": ["DIRECT"]}
        }
    })";
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QVector<cq::ProxyGroup> groups =
        MihomoApi::parseProxyGroups(root.value(QStringLiteral("proxies")).toObject());
    QCOMPARE(groups.size(), 1);
    QCOMPARE(groups.first().name, QStringLiteral("Visible"));
}

void ApiParsersTest::parsesConnections()
{
    const QByteArray json = R"({
        "uploadTotal": 4096,
        "downloadTotal": 8192,
        "connections": [
            {
                "id": "abc",
                "metadata": {
                    "network": "tcp",
                    "type": "HTTPS",
                    "host": "api.example.com",
                    "destinationPort": "443"
                },
                "upload": 128,
                "download": 2048,
                "start": "2026-01-01T10:00:00.000Z",
                "chains": ["节点选择", "香港 01"],
                "rule": "DOMAIN-SUFFIX"
            }
        ]
    })";
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    qint64 up = 0;
    qint64 down = 0;
    const auto connections = MihomoApi::parseConnections(root, &up, &down);
    QCOMPARE(up, 4096LL);
    QCOMPARE(down, 8192LL);
    QCOMPARE(connections.size(), 1);
    const cq::ConnectionInfo& info = connections.first();
    QCOMPARE(info.host, QStringLiteral("api.example.com:443"));
    QCOMPARE(info.network, QStringLiteral("tcp"));
    QCOMPARE(info.type, QStringLiteral("HTTPS"));
    QCOMPARE(info.rule, QStringLiteral("DOMAIN-SUFFIX"));
    QCOMPARE(info.chain, QStringLiteral("节点选择 → 香港 01"));
    QCOMPARE(info.upload, 128LL);
    QCOMPARE(info.download, 2048LL);
    QVERIFY(info.startedAt.isValid());
}

QTEST_GUILESS_MAIN(ApiParsersTest)

#include "test_api_parsers.moc"
