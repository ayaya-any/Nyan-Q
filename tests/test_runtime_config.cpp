#include "core/RuntimeConfig.h"

#include "scripts/YamlDocument.h"

#include <QTemporaryDir>
#include <QTest>
#include <QVector>

class RuntimeConfigTest : public QObject {
    Q_OBJECT

private slots:
    void overridesControlledTopLevelKeys();
    void preservesExistingDnsAndAddsTun();
    void addsDnsWhenTunEnabledWithoutDns();
    void quotesControllerSecret();
    void writeFailsForMissingSource();

    void preservesTunRouteExcludes();
    void readsRouteExcludesFromEveryYamlStyle();
    void mergesDeprecatedInetRouteKeys();
    void rejectsInvalidRouteExcludeEntries();
    void preservesOtherTunKeys();
    void defaultsTunStackToMipsAndKeepsUserStack();
    void deduplicatesRouteExcludeEntries();
    void hardensInjectedDns();
    void reportsAppliedRouteExcludeCount();
    void fallsBackWhenProfileIsNotSingleYamlDocument();
    void acceptsQuotedTunKeyAndSpacedColon();
    void skipsDnsHijackWhenProfileDnsDisabled();
    void rejectsEnabledDnsWithoutNameserver();
    void rejectsInvalidTunScalarTypes();
    void warnsAboutDisabledIpv6AndCatchAllExclusions();
    void warnsAboutFakeDnsWithRouteExcludes();
};

namespace {
cq::AppSettings baseSettings()
{
    cq::AppSettings settings;
    settings.mixedPort = 17890;
    settings.controllerPort = 19090;
    settings.secret = QStringLiteral("it's-a-secret");
    settings.mode = QStringLiteral("rule");
    settings.logLevel = QStringLiteral("info");
    return settings;
}

cq::AppSettings tunSettings()
{
    cq::AppSettings settings = baseSettings();
    settings.tunEnabled = true;
    return settings;
}

QString sampleYaml()
{
    return QStringLiteral(R"(mixed-port: 7890
port: 7891
socks-port: 7892
allow-lan: true
mode: global
log-level: debug
external-controller: 0.0.0.0:9090
secret: "old"
ipv6: true

proxies:
  - name: test
    type: ss
    server: example.com
    port: 443
    cipher: aes-128-gcm
    password: pass

proxy-groups:
  - name: PROXY
    type: select
    proxies: [test, DIRECT]

rules:
  - MATCH,PROXY
)");
}

nq::yaml::Node parseRuntime(const QString& yaml)
{
    nq::yaml::Document document;
    if (!document.parse(yaml.toUtf8()))
        return nq::yaml::Node::null();
    return document.root;
}

const nq::yaml::Node* tunOf(const nq::yaml::Node& root)
{
    return root.find(QStringLiteral("tun"));
}

QStringList sequenceValues(const nq::yaml::Node* node)
{
    QStringList values;
    if (!node || !node->isSequence())
        return values;
    for (const nq::yaml::Node& item : node->sequence)
        values.append(item.value);
    return values;
}

QStringList routeExcludesOf(const nq::yaml::Node& root)
{
    const nq::yaml::Node* tun = tunOf(root);
    return tun ? sequenceValues(tun->find(QStringLiteral("route-exclude-address")))
               : QStringList();
}
}  // namespace

void RuntimeConfigTest::overridesControlledTopLevelKeys()
{
    const cq::AppSettings settings = baseSettings();
    const QString result = RuntimeConfig::build(sampleYaml(), settings);

    QCOMPARE(result.count(QStringLiteral("mixed-port:")), 1);
    QVERIFY(result.contains(QStringLiteral("mixed-port: 17890")));
    QVERIFY(result.contains(QStringLiteral("allow-lan: false")));
    QVERIFY(result.contains(QStringLiteral("mode: rule")));
    QVERIFY(result.contains(QStringLiteral("log-level: info")));
    QVERIFY(result.contains(QStringLiteral("ipv6: false")));
    QVERIFY(result.contains(QStringLiteral("external-controller: '127.0.0.1:19090'")));
    QVERIFY(result.contains(QStringLiteral("secret: 'it''s-a-secret'")));
    QVERIFY(!result.contains(QStringLiteral("port: 7891")));
    QVERIFY(!result.contains(QStringLiteral("socks-port: 7892")));
    QVERIFY(!result.contains(QStringLiteral("old")));
    QVERIFY(result.contains(QStringLiteral("proxies:")));
    QVERIFY(result.contains(QStringLiteral("MATCH,PROXY")));
    QCOMPARE(result.count(QStringLiteral("external-controller:")), 1);
    QCOMPARE(result.count(QStringLiteral("tun:")), 0);
}

void RuntimeConfigTest::preservesExistingDnsAndAddsTun()
{
    const QString yaml = sampleYaml() +
                         QStringLiteral("\ndns:\n  enable: true\n  listen: 0.0.0.0:1053\n"
                                        "  nameserver:\n    - 223.5.5.5\n");
    const QString result = RuntimeConfig::build(yaml, tunSettings());

    QVERIFY(result.contains(QStringLiteral("tun:")));
    QCOMPARE(result.count(QStringLiteral("dns:")), 1);
    QVERIFY(result.contains(QStringLiteral("listen: 0.0.0.0:1053")));
    QVERIFY(!result.contains(QStringLiteral("fake-ip-range: 198.18.0.1/16")));

    const nq::yaml::Node root = parseRuntime(result);
    QVERIFY(root.isMapping());
    const nq::yaml::Node* tun = tunOf(root);
    QVERIFY(tun);
    QVERIFY(tun->find(QStringLiteral("dns-hijack")));
}

void RuntimeConfigTest::addsDnsWhenTunEnabledWithoutDns()
{
    const QString result = RuntimeConfig::build(sampleYaml(), tunSettings());

    QCOMPARE(result.count(QStringLiteral("dns:")), 1);
    QVERIFY(result.contains(QStringLiteral("fake-ip-range: 198.18.0.1/16")));
    QVERIFY(result.contains(QStringLiteral("listen: 127.0.0.1:1053")));
    QVERIFY(result.contains(QStringLiteral("proxy-server-nameserver:")));

    const nq::yaml::Node root = parseRuntime(result);
    QVERIFY(root.isMapping());
    const nq::yaml::Node* dns = root.find(QStringLiteral("dns"));
    QVERIFY(dns);
    QCOMPARE(dns->find(QStringLiteral("listen"))->value,
             QStringLiteral("127.0.0.1:1053"));
    QVERIFY(!sequenceValues(dns->find(QStringLiteral("proxy-server-nameserver"))).isEmpty());
}

void RuntimeConfigTest::quotesControllerSecret()
{
    cq::AppSettings settings = baseSettings();
    settings.secret = QStringLiteral("line'break");
    const QString result = RuntimeConfig::build(sampleYaml(), settings);
    QVERIFY(result.contains(QStringLiteral("secret: 'line''break'")));
}

void RuntimeConfigTest::writeFailsForMissingSource()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    const bool ok = RuntimeConfig::write(dir.filePath(QStringLiteral("missing.yaml")),
                                          dir.filePath(QStringLiteral("runtime.yaml")),
                                          baseSettings(), &error);
    QVERIFY(!ok);
    QVERIFY(!error.isEmpty());
}

void RuntimeConfigTest::preservesTunRouteExcludes()
{
    const QString yaml = QStringLiteral(R"(tun:
  enable: true
  route-exclude-address:
    - 192.168.1.0/24
    - '203.0.113.10/32'
)");
    QStringList warnings;
    const QString result = RuntimeConfig::build(yaml, tunSettings(), nullptr, &warnings);
    QVERIFY(!result.isEmpty());
    QCOMPARE(result.count(QStringLiteral("route-exclude-address:")), 1);

    const nq::yaml::Node root = parseRuntime(result);
    QVERIFY(root.isMapping());
    QCOMPARE(routeExcludesOf(root),
             (QStringList{QStringLiteral("192.168.1.0/24"),
                          QStringLiteral("203.0.113.10/32")}));
    QVERIFY(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("TUN 托管设置")));
}

void RuntimeConfigTest::readsRouteExcludesFromEveryYamlStyle()
{
    const QVector<QPair<QString, QStringList>> cases = {
        {QStringLiteral("tun:\n"
                        "  enable: true\n"
                        "  route-exclude-address: [192.168.1.0/24, '203.0.113.10/32']\n"),
         {QStringLiteral("192.168.1.0/24"), QStringLiteral("203.0.113.10/32")}},
        {QStringLiteral("tun: {enable: true, route-exclude-address: [198.51.100.0/24]}\n"),
         {QStringLiteral("198.51.100.0/24")}},
        {QStringLiteral("base: &base\n"
                        "  route-exclude-address:\n"
                        "    - 192.0.2.0/24\n"
                        "tun:\n"
                        "  <<: *base\n"
                        "  enable: true\n"),
         {QStringLiteral("192.0.2.0/24")}},
        {QStringLiteral("tun:\n"
                        "  enable: true\n"
                        "  route-exclude-address:\n"
                        "    - 10.0.0.0/8 # lan\n"
                        "    # - 172.16.0.0/12\n"
                        "    - 203.0.113.10/32\n"),
         {QStringLiteral("10.0.0.0/8"), QStringLiteral("203.0.113.10/32")}},
        {QStringLiteral("tun:\n"
                        "  enable: true\n"
                        "  route-exclude-address:\n"
                        "    - \"198.18.0.0/15\"\n"
                        "    - fc00::/7\n"),
         {QStringLiteral("198.18.0.0/15"), QStringLiteral("fc00::/7")}},
    };

    for (const auto& [source, expected] : cases) {
        QString error;
        const QString result = RuntimeConfig::build(source, tunSettings(), &error);
        QVERIFY2(!result.isEmpty(), qPrintable(error));
        const nq::yaml::Node root = parseRuntime(result);
        QVERIFY2(root.isMapping(), qPrintable(source));
        QCOMPARE(routeExcludesOf(root), expected);
    }
}

void RuntimeConfigTest::mergesDeprecatedInetRouteKeys()
{
    const QString yaml = QStringLiteral(R"(tun:
  enable: true
  inet4-route-exclude-address:
    - 10.0.0.0/8
    - 192.0.2.0/24
  inet6-route-exclude-address:
    - fc00::/7
    - 192.0.2.0/24
)");
    const QString result = RuntimeConfig::build(yaml, tunSettings());
    QVERIFY(!result.isEmpty());

    const nq::yaml::Node root = parseRuntime(result);
    QVERIFY(root.isMapping());
    QCOMPARE(routeExcludesOf(root),
             (QStringList{QStringLiteral("10.0.0.0/8"),
                          QStringLiteral("192.0.2.0/24"),
                          QStringLiteral("fc00::/7")}));

    const nq::yaml::Node* tun = tunOf(root);
    QVERIFY(tun);
    QVERIFY(!tun->find(QStringLiteral("inet4-route-exclude-address")));
    QVERIFY(!tun->find(QStringLiteral("inet6-route-exclude-address")));
}

void RuntimeConfigTest::rejectsInvalidRouteExcludeEntries()
{
    const QVector<QString> invalid = {
        QStringLiteral("tun:\n  route-exclude-address:\n    - not-a-cidr\n"),
        QStringLiteral("tun:\n  route-exclude-address:\n    - 300.1.1.1/32\n"),
        QStringLiteral("tun:\n  route-exclude-address:\n    - 10.0.0.0/33\n"),
        QStringLiteral("tun:\n  route-exclude-address:\n    - fc00::/129\n"),
        QStringLiteral("tun:\n  route-exclude-address: 192.168.1.0/24\n"),
        QStringLiteral("tun:\n  route-exclude-address:\n    - 192.168.1.0\n"),
    };

    for (const QString& source : invalid) {
        QString error;
        const QString result = RuntimeConfig::build(source, tunSettings(), &error);
        QVERIFY2(result.isEmpty(), qPrintable(source));
        QVERIFY2(error.contains(QStringLiteral("route-exclude-address")),
                 qPrintable(error));
    }

    QString routeError;
    const QString routeResult = RuntimeConfig::build(
        QStringLiteral("tun:\n  route-address:\n    - bogus\n"), tunSettings(),
        &routeError);
    QVERIFY(routeResult.isEmpty());
    QVERIFY(routeError.contains(QStringLiteral("route-address")));
}

void RuntimeConfigTest::preservesOtherTunKeys()
{
    const QString yaml = QStringLiteral(R"(tun:
  enable: false
  auto-route: false
  stack: gvisor
  mtu: 1400
  strict-route: true
  device: NyanTun
  udp-timeout: 300
  endpoint-independent-nat: true
  dns-hijack:
    - 0.0.0.0:53
  route-address:
    - 0.0.0.0/1
    - 128.0.0.0/1
  route-exclude-address:
    - 192.168.0.0/16
)");
    const QString result = RuntimeConfig::build(yaml, tunSettings());
    QVERIFY(!result.isEmpty());

    const nq::yaml::Node root = parseRuntime(result);
    QVERIFY(root.isMapping());
    const nq::yaml::Node* tun = tunOf(root);
    QVERIFY(tun);
    QCOMPARE(tun->find(QStringLiteral("enable"))->scalarType, nq::yaml::ScalarType::Bool);
    QCOMPARE(tun->find(QStringLiteral("enable"))->value, QStringLiteral("true"));
    QCOMPARE(tun->find(QStringLiteral("auto-route"))->value, QStringLiteral("true"));
    QCOMPARE(tun->find(QStringLiteral("stack"))->value, QStringLiteral("gvisor"));
    QCOMPARE(tun->find(QStringLiteral("mtu"))->value, QStringLiteral("1400"));
    QCOMPARE(tun->find(QStringLiteral("strict-route"))->value, QStringLiteral("true"));
    QCOMPARE(tun->find(QStringLiteral("device"))->value, QStringLiteral("NyanTun"));
    QCOMPARE(tun->find(QStringLiteral("udp-timeout"))->value, QStringLiteral("300"));
    QCOMPARE(tun->find(QStringLiteral("endpoint-independent-nat"))->value,
             QStringLiteral("true"));

    const QStringList hijack = sequenceValues(tun->find(QStringLiteral("dns-hijack")));
    QVERIFY(hijack.contains(QStringLiteral("any:53")));
    QVERIFY(hijack.contains(QStringLiteral("tcp://any:53")));
    QVERIFY(hijack.contains(QStringLiteral("0.0.0.0:53")));
    QCOMPARE(sequenceValues(tun->find(QStringLiteral("route-address"))),
             (QStringList{QStringLiteral("0.0.0.0/1"), QStringLiteral("128.0.0.0/1")}));
    QCOMPARE(routeExcludesOf(root), (QStringList{QStringLiteral("192.168.0.0/16")}));
}

void RuntimeConfigTest::defaultsTunStackToMipsAndKeepsUserStack()
{
    const QString defaultResult = RuntimeConfig::build(sampleYaml(), tunSettings());
    const nq::yaml::Node defaultRoot = parseRuntime(defaultResult);
    QVERIFY(defaultRoot.isMapping());
    QCOMPARE(tunOf(defaultRoot)->find(QStringLiteral("stack"))->value,
             QStringLiteral("mips"));

    const QString customResult = RuntimeConfig::build(
        sampleYaml() + QStringLiteral("\ntun:\n  enable: true\n  stack: system\n"),
        tunSettings());
    const nq::yaml::Node customRoot = parseRuntime(customResult);
    QVERIFY(customRoot.isMapping());
    QCOMPARE(tunOf(customRoot)->find(QStringLiteral("stack"))->value,
             QStringLiteral("system"));
}

void RuntimeConfigTest::deduplicatesRouteExcludeEntries()
{
    const QString yaml = QStringLiteral(R"(tun:
  enable: true
  route-exclude-address:
    - 192.168.1.0/24
    - 192.168.1.0/24
    - '203.0.113.10/32'
    - 203.0.113.10/32
)");
    const QString result = RuntimeConfig::build(yaml, tunSettings());
    const nq::yaml::Node root = parseRuntime(result);
    QVERIFY(root.isMapping());
    QCOMPARE(routeExcludesOf(root),
             (QStringList{QStringLiteral("192.168.1.0/24"),
                          QStringLiteral("203.0.113.10/32")}));
}

void RuntimeConfigTest::hardensInjectedDns()
{
    const QString result = RuntimeConfig::build(sampleYaml(), tunSettings());
    const nq::yaml::Node root = parseRuntime(result);
    QVERIFY(root.isMapping());

    const nq::yaml::Node* dns = root.find(QStringLiteral("dns"));
    QVERIFY(dns);
    QCOMPARE(dns->find(QStringLiteral("listen"))->value,
             QStringLiteral("127.0.0.1:1053"));
    QVERIFY(!sequenceValues(dns->find(QStringLiteral("proxy-server-nameserver"))).isEmpty());

    const QStringList hijack =
        sequenceValues(tunOf(root)->find(QStringLiteral("dns-hijack")));
    QVERIFY(hijack.contains(QStringLiteral("any:53")));
    QVERIFY(hijack.contains(QStringLiteral("tcp://any:53")));
}

void RuntimeConfigTest::reportsAppliedRouteExcludeCount()
{
    QStringList warnings;
    const QString result = RuntimeConfig::build(
        QStringLiteral("tun:\n"
                       "  route-exclude-address:\n"
                       "    - 192.168.1.0/24\n"
                       "    - 203.0.113.10/32\n"),
        tunSettings(), nullptr, &warnings);
    QVERIFY(!result.isEmpty());
    QVERIFY2(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("2 条")),
             qPrintable(warnings.join(QLatin1Char('\n'))));
}

void RuntimeConfigTest::fallsBackWhenProfileIsNotSingleYamlDocument()
{
    QStringList warnings;
    const QString result = RuntimeConfig::build(
        QStringLiteral("tun:\n"
                       "  enable: true\n"
                       "  route-exclude-address:\n"
                       "    - 192.168.1.0/24\n"
                       "---\n"
                       "other: true\n"),
        tunSettings(), nullptr, &warnings);
    QVERIFY(!result.isEmpty());
    QVERIFY2(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("跳过")),
             qPrintable(warnings.join(QLatin1Char('\n'))));

    const nq::yaml::Node root = parseRuntime(result);
    QVERIFY(root.isMapping());
    QVERIFY(tunOf(root));
    QVERIFY(routeExcludesOf(root).isEmpty());
}

void RuntimeConfigTest::acceptsQuotedTunKeyAndSpacedColon()
{
    const QVector<QString> sources = {
        QStringLiteral("'tun':\n"
                       "  enable: true\n"
                       "  route-exclude-address:\n"
                       "    - 192.168.1.0/24\n"),
        QStringLiteral("tun :\n"
                       "  enable: true\n"
                       "  route-exclude-address:\n"
                       "    - 192.168.1.0/24\n"),
    };
    for (const QString& source : sources) {
        const QString result = RuntimeConfig::build(source, tunSettings());
        QVERIFY2(!result.isEmpty(), qPrintable(source));
        const nq::yaml::Node root = parseRuntime(result);
        QVERIFY2(root.isMapping(), qPrintable(source));
        QCOMPARE(routeExcludesOf(root), (QStringList{QStringLiteral("192.168.1.0/24")}));
        QCOMPARE(result.count(QStringLiteral("tun:")), 1);
    }
}

void RuntimeConfigTest::skipsDnsHijackWhenProfileDnsDisabled()
{
    const QString yaml = sampleYaml() +
                         QStringLiteral("dns:\n  enable: false\n  nameserver:\n"
                                        "    - 223.5.5.5\n");
    QStringList warnings;
    const QString result = RuntimeConfig::build(yaml, tunSettings(), nullptr, &warnings);
    QVERIFY(!result.isEmpty());

    const nq::yaml::Node root = parseRuntime(result);
    QVERIFY(root.isMapping());
    const nq::yaml::Node* tun = tunOf(root);
    QVERIFY(tun);
    QVERIFY(!tun->find(QStringLiteral("dns-hijack")));
    const nq::yaml::Node* dns = root.find(QStringLiteral("dns"));
    QVERIFY(dns);
    QCOMPARE(dns->find(QStringLiteral("enable"))->value, QStringLiteral("false"));
    QCOMPARE(result.count(QStringLiteral("dns:")), 1);
    QVERIFY2(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("未启用")),
             qPrintable(warnings.join(QLatin1Char('\n'))));
}

void RuntimeConfigTest::rejectsEnabledDnsWithoutNameserver()
{
    QString error;
    const QString result = RuntimeConfig::build(
        sampleYaml() + QStringLiteral("dns:\n  enable: true\n"), tunSettings(), &error);
    QVERIFY(result.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("nameserver")), qPrintable(error));

    QString typeError;
    const QString badType = RuntimeConfig::build(
        sampleYaml() +
            QStringLiteral("dns:\n  enable: \"true\"\n  nameserver:\n    - 223.5.5.5\n"),
        tunSettings(), &typeError);
    QVERIFY(badType.isEmpty());
    QVERIFY2(typeError.contains(QStringLiteral("enable")), qPrintable(typeError));

    QString entryError;
    const QString badEntry = RuntimeConfig::build(
        sampleYaml() +
            QStringLiteral("dns:\n  enable: true\n  nameserver:\n    - {a: b}\n"),
        tunSettings(), &entryError);
    QVERIFY(badEntry.isEmpty());
    QVERIFY2(entryError.contains(QStringLiteral("nameserver")), qPrintable(entryError));
}

void RuntimeConfigTest::rejectsInvalidTunScalarTypes()
{
    const QVector<QPair<QString, QString>> invalid = {
        {QStringLiteral("tun:\n  stack: mipz\n"), QStringLiteral("stack")},
        {QStringLiteral("tun:\n  mtu: \"1400\"\n"), QStringLiteral("mtu")},
        {QStringLiteral("tun:\n  mtu: -1\n"), QStringLiteral("mtu")},
        {QStringLiteral("tun:\n  gso: 1\n"), QStringLiteral("gso")},
        {QStringLiteral("tun:\n  strict-route: \"true\"\n"), QStringLiteral("strict-route")},
        {QStringLiteral("tun:\n  dns-mode: bogus\n"), QStringLiteral("dns-mode")},
    };
    for (const auto& [source, key] : invalid) {
        QString error;
        const QString result = RuntimeConfig::build(source, tunSettings(), &error);
        QVERIFY2(result.isEmpty(), qPrintable(source));
        QVERIFY2(error.contains(key), qPrintable(error));
    }

    const QString accepted = RuntimeConfig::build(
        QStringLiteral("tun:\n  stack: system\n  mtu: 1400\n"
                       "  strict-route: true\n  dns-mode: hijack\n"),
        tunSettings());
    QVERIFY(!accepted.isEmpty());
    const nq::yaml::Node root = parseRuntime(accepted);
    QVERIFY(root.isMapping());
    const nq::yaml::Node* tun = tunOf(root);
    QVERIFY(tun);
    QCOMPARE(tun->find(QStringLiteral("stack"))->value, QStringLiteral("system"));
    QCOMPARE(tun->find(QStringLiteral("mtu"))->value, QStringLiteral("1400"));
    QCOMPARE(tun->find(QStringLiteral("strict-route"))->value, QStringLiteral("true"));
    QCOMPARE(tun->find(QStringLiteral("dns-mode"))->value, QStringLiteral("hijack"));
}

void RuntimeConfigTest::warnsAboutDisabledIpv6AndCatchAllExclusions()
{
    const QString yaml = QStringLiteral(
        "tun:\n  route-exclude-address:\n    - 0.0.0.0/0\n    - '::/0'\n"
        "    - 2001:db8::/32\n");
    QStringList warnings;
    const QString result = RuntimeConfig::build(yaml, tunSettings(), nullptr, &warnings);
    QVERIFY(!result.isEmpty());
    QString text = warnings.join(QLatin1Char('\n'));
    QVERIFY2(text.contains(QStringLiteral("无法接管")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("IPv6")), qPrintable(text));

    cq::AppSettings ipv6Settings = tunSettings();
    ipv6Settings.ipv6 = true;
    QStringList ipv6Warnings;
    const QString ipv6Result =
        RuntimeConfig::build(yaml, ipv6Settings, nullptr, &ipv6Warnings);
    QVERIFY(!ipv6Result.isEmpty());
    QVERIFY2(!ipv6Warnings.join(QLatin1Char('\n')).contains(QStringLiteral("IPv6")),
             qPrintable(ipv6Warnings.join(QLatin1Char('\n'))));
}

void RuntimeConfigTest::warnsAboutFakeDnsWithRouteExcludes()
{
    QStringList warnings;
    const QString result = RuntimeConfig::build(
        QStringLiteral("tun:\n  route-exclude-address:\n    - 192.168.1.0/24\n"),
        tunSettings(), nullptr, &warnings);
    QVERIFY(!result.isEmpty());
    const QString text = warnings.join(QLatin1Char('\n'));
    QVERIFY2(text.contains(QStringLiteral("fake-ip")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("直接连接 IP")), qPrintable(text));
}

QTEST_GUILESS_MAIN(RuntimeConfigTest)

#include "test_runtime_config.moc"
