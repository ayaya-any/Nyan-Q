#include "scripts/YamlDocument.h"

#include "yaml_sample.h"

#include <QTest>

using namespace nq::yaml;

class YamlDocumentTest : public QObject {
    Q_OBJECT

private slots:
    void parsesMihomoConfigAndPreservesFields();
    void preservesMappingOrder();
    void roundTripsTypesAndSpecialStrings();
    void resolvesAnchorsAndMergeKeys();
    void reportsSyntaxErrors();
    void rejectsMultipleDocuments();
    void diffReportsChangedPaths();
    void handlesQuotedNumericStrings();
    void keepsUnknownTags();
    void preservesFloatingPointType();
};

void YamlDocumentTest::parsesMihomoConfigAndPreservesFields()
{
    Document document;
    QVERIFY2(document.parse(yamlSampleConfig().toUtf8()), qPrintable(document.error.toString()));
    QVERIFY(document.root.isMapping());

    const Node* proxies = document.root.find(QStringLiteral("proxies"));
    QVERIFY(proxies);
    QCOMPARE(proxies->sequence.size(), 2);
    QCOMPARE(proxies->sequence.first().find(QStringLiteral("name"))->value,
             QStringLiteral("香港 · 01"));
    QCOMPARE(proxies->sequence.first().find(QStringLiteral("port"))->scalarType,
             ScalarType::Int);
    QCOMPARE(proxies->sequence.first()
                 .find(QStringLiteral("plugin-opts"))
                 ->find(QStringLiteral("mode"))
                 ->value,
             QStringLiteral("tls"));

    const Node* dns = document.root.find(QStringLiteral("dns"));
    QVERIFY(dns);
    QCOMPARE(dns->find(QStringLiteral("fallback-filter"))
                 ->find(QStringLiteral("geoip"))
                 ->scalarType,
             ScalarType::Bool);
    QCOMPARE(dns->find(QStringLiteral("nameserver"))->sequence.size(), 2);

    const Node* custom = document.root.find(QStringLiteral("custom-extension"));
    QVERIFY(custom);
    QCOMPARE(custom->find(QStringLiteral("keep"))->value, QStringLiteral("yes"));
    QCOMPARE(custom->find(QStringLiteral("nested"))->sequence.size(), 2);
    QCOMPARE(custom->find(QStringLiteral("nested"))
                 ->sequence.first()
                 .find(QStringLiteral("b"))
                 ->value,
             QStringLiteral("2"));
}

void YamlDocumentTest::preservesMappingOrder()
{
    Document document;
    QVERIFY(document.parse(yamlSampleConfig().toUtf8()));
    QStringList keys;
    for (const auto& entry : document.root.mapping)
        keys.append(entry.first);
    QCOMPARE(keys.first(), QStringLiteral("mixed-port"));
    QCOMPARE(keys.at(3), QStringLiteral("unified-delay"));
    QCOMPARE(keys.last(), QStringLiteral("custom-extension"));

    const Node* dns = document.root.find(QStringLiteral("dns"));
    QStringList dnsKeys;
    for (const auto& entry : dns->mapping)
        dnsKeys.append(entry.first);
    QCOMPARE(dnsKeys.at(1), QStringLiteral("enhanced-mode"));
    QCOMPARE(dnsKeys.last(), QStringLiteral("fallback-filter"));
}

void YamlDocumentTest::roundTripsTypesAndSpecialStrings()
{
    Document document;
    QVERIFY(document.parse(yamlSampleConfig().toUtf8()));
    const QByteArray dumped = document.dump();
    QVERIFY(!dumped.isEmpty());

    Document reparsed;
    QVERIFY2(reparsed.parse(dumped), qPrintable(reparsed.error.toString()));
    QCOMPARE(reparsed.root.find(QStringLiteral("mixed-port"))->scalarType, ScalarType::Int);
    QCOMPARE(reparsed.root.find(QStringLiteral("unified-delay"))->scalarType, ScalarType::Bool);
    QCOMPARE(reparsed.root.find(QStringLiteral("secret"))->value, QStringLiteral("s3cret"));
    QCOMPARE(reparsed.root.find(QStringLiteral("secret"))->scalarType, ScalarType::String);
    QCOMPARE(reparsed.root.find(QStringLiteral("proxies"))
                 ->sequence.first()
                 .find(QStringLiteral("password"))
                 ->value,
             QStringLiteral("p@ss word"));
    QCOMPARE(reparsed.root.find(QStringLiteral("proxies"))
                 ->sequence.first()
                 .find(QStringLiteral("server"))
                 ->scalarType,
             ScalarType::String);
    QCOMPARE(reparsed.root.find(QStringLiteral("custom-extension"))
                 ->find(QStringLiteral("keep"))
                 ->value,
             QStringLiteral("yes"));

    const DiffResult diff = nq::yaml::diff(document.root, reparsed.root);
    QVERIFY2(diff.entries.isEmpty(), qPrintable(diff.entries.isEmpty()
                                                    ? QString()
                                                    : diff.entries.first().path));
}

void YamlDocumentTest::resolvesAnchorsAndMergeKeys()
{
    const QString yaml = QStringLiteral(R"(defaults: &defaults
  type: ss
  cipher: aes-128-gcm
  udp: true
proxies:
  - <<: *defaults
    name: a
    server: a.example.com
    port: 443
    password: one
  - <<: *defaults
    name: b
    server: b.example.com
    port: 443
    password: two
  - name: c
    type: ss
    server: c.example.com
    port: 443
    cipher: aes-128-gcm
    password: three
    udp: false
)");
    Document document;
    QVERIFY2(document.parse(yaml.toUtf8()), qPrintable(document.error.toString()));
    const Node* proxies = document.root.find(QStringLiteral("proxies"));
    QVERIFY(proxies);
    QCOMPARE(proxies->sequence.size(), 3);
    QCOMPARE(proxies->sequence.at(0).find(QStringLiteral("cipher"))->value,
             QStringLiteral("aes-128-gcm"));
    QCOMPARE(proxies->sequence.at(1).find(QStringLiteral("udp"))->scalarType,
             ScalarType::Bool);
    QCOMPARE(proxies->sequence.at(2).find(QStringLiteral("udp"))->value,
             QStringLiteral("false"));
    QVERIFY(!document.root.find(QStringLiteral("defaults"))
                 ->find(QStringLiteral("<<")));
}

void YamlDocumentTest::reportsSyntaxErrors()
{
    Document document;
    QVERIFY(!document.parse(QStringLiteral("proxies:\n  - name: [unclosed\n").toUtf8()));
    QVERIFY(!document.error.message.isEmpty());
    QVERIFY(document.error.line > 0);

    Document empty;
    QVERIFY(!empty.parse(QByteArray()));
    QVERIFY(!empty.error.message.isEmpty());
}

void YamlDocumentTest::rejectsMultipleDocuments()
{
    Document document;
    QVERIFY(!document.parse(QStringLiteral("a: 1\n---\nb: 2\n").toUtf8()));
    QVERIFY(document.error.message.contains(QStringLiteral("多个")));
}

void YamlDocumentTest::diffReportsChangedPaths()
{
    Document before;
    Document after;
    QVERIFY(before.parse(yamlSampleConfig().toUtf8()));
    QVERIFY(after.parse(yamlSampleConfig().toUtf8()));

    after.root.set(QStringLiteral("mixed-port"), Node::integer(17890));
    after.root.find(QStringLiteral("dns"))->set(QStringLiteral("enable"), Node::boolean(false));
    Node* rules = &after.root.set(QStringLiteral("rules"), Node::array());
    rules->sequence.append(Node::string(QStringLiteral("MATCH,DIRECT")));

    const DiffResult result = nq::yaml::diff(before.root, after.root);
    QStringList changed;
    for (const DiffEntry& entry : result.entries) {
        if (entry.kind == DiffEntry::Kind::Changed)
            changed.append(entry.path);
    }
    QVERIFY(changed.contains(QStringLiteral("mixed-port")));
    QVERIFY(changed.contains(QStringLiteral("dns.enable")));
    QVERIFY(result.total >= 3);
}

void YamlDocumentTest::handlesQuotedNumericStrings()
{
    Document document;
    QVERIFY(document.parse(QStringLiteral("a: \"123\"\nb: 123\nc: 'true'\nd: true\n").toUtf8()));
    QCOMPARE(document.root.find(QStringLiteral("a"))->scalarType, ScalarType::String);
    QCOMPARE(document.root.find(QStringLiteral("b"))->scalarType, ScalarType::Int);
    QCOMPARE(document.root.find(QStringLiteral("c"))->scalarType, ScalarType::String);
    QCOMPARE(document.root.find(QStringLiteral("d"))->scalarType, ScalarType::Bool);

    Document reparsed;
    QVERIFY(reparsed.parse(document.dump()));
    QCOMPARE(reparsed.root.find(QStringLiteral("a"))->scalarType, ScalarType::String);
    QCOMPARE(reparsed.root.find(QStringLiteral("c"))->scalarType, ScalarType::String);
}

void YamlDocumentTest::keepsUnknownTags()
{
    Document document;
    QVERIFY2(document.parse(QStringLiteral("value: !env HOME\nlist: !custom [a, b]\n").toUtf8()),
             qPrintable(document.error.toString()));
    QCOMPARE(document.root.find(QStringLiteral("value"))->tag, QStringLiteral("!env"));
    QCOMPARE(document.root.find(QStringLiteral("list"))->tag, QStringLiteral("!custom"));

    Document reparsed;
    QVERIFY2(reparsed.parse(document.dump()), qPrintable(reparsed.error.toString()));
    QCOMPARE(reparsed.root.find(QStringLiteral("value"))->tag, QStringLiteral("!env"));
    QCOMPARE(reparsed.root.find(QStringLiteral("value"))->value, QStringLiteral("HOME"));
}

void YamlDocumentTest::preservesFloatingPointType()
{
    Document document;
    QVERIFY2(document.parse(QStringLiteral("ratio: 1.0\nfactor: 1e3\nport: 443\n").toUtf8()),
             qPrintable(document.error.toString()));
    QCOMPARE(document.root.find(QStringLiteral("ratio"))->scalarType, ScalarType::Double);
    QCOMPARE(document.root.find(QStringLiteral("factor"))->scalarType, ScalarType::Double);
    QCOMPARE(document.root.find(QStringLiteral("port"))->scalarType, ScalarType::Int);

    Document reparsed;
    QVERIFY(reparsed.parse(document.dump()));
    QCOMPARE(reparsed.root.find(QStringLiteral("ratio"))->scalarType, ScalarType::Double);
    QCOMPARE(reparsed.root.find(QStringLiteral("factor"))->scalarType, ScalarType::Double);
    QCOMPARE(reparsed.root.find(QStringLiteral("port"))->scalarType, ScalarType::Int);
}

QTEST_GUILESS_MAIN(YamlDocumentTest)

#include "test_yaml_document.moc"

