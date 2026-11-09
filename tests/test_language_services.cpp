#include "lang/LanguageRegistry.h"

#include <QTest>

using namespace nq::lang;

namespace {
QStringList labels(const QVector<CompletionItem>& items)
{
    QStringList result;
    for (const CompletionItem& item : items)
        result.append(item.label);
    return result;
}

CompletionRequest requestAtEnd(const QString& text)
{
    CompletionRequest request;
    request.text = text;
    request.position = text.size();
    return request;
}
}  // namespace

class LanguageServicesTest : public QObject {
    Q_OBJECT

private slots:
    void registryResolvesByExtensionAndId();
    void yamlReportsSyntaxErrors();
    void yamlReportsDuplicateKeys();
    void yamlCompletesTopLevelAndSections();
    void yamlCompletesValuesAndRules();
    void javascriptReportsSyntaxErrors();
    void javascriptWarnsMissingMain();
    void javascriptCompletesKeywordsAndConfig();
};

void LanguageServicesTest::registryResolvesByExtensionAndId()
{
    LanguageRegistry& registry = LanguageRegistry::instance();
    QVERIFY(registry.providerById(QStringLiteral("yaml")));
    QVERIFY(registry.providerById(QStringLiteral("javascript")));
    QCOMPARE(registry.providerForPath(QStringLiteral("profile.yaml"))->id(),
             QStringLiteral("yaml"));
    QCOMPARE(registry.providerForPath(QStringLiteral("profile.yml"))->id(),
             QStringLiteral("yaml"));
    QCOMPARE(registry.providerForPath(QStringLiteral("script.js"))->id(),
             QStringLiteral("javascript"));
    QCOMPARE(registry.providerForPath(QStringLiteral("script.mjs"))->id(),
             QStringLiteral("javascript"));
    QVERIFY(registry.providers().size() >= 2);
}

void LanguageServicesTest::yamlReportsSyntaxErrors()
{
    LanguageProvider* yaml = LanguageRegistry::instance().providerById(QStringLiteral("yaml"));
    QVERIFY(yaml);

    QVERIFY(yaml->diagnose(QStringLiteral("mixed-port: 7890\n"
                                          "dns:\n"
                                          "  nameserver:\n"
                                          "    - 223.5.5.5\n"
                                          "    - 1.1.1.1\n"
                                          "proxies:\n"
                                          "  - name: A\n"
                                          "    type: ss\n"
                                          "  - name: B\n"
                                          "    type: ss\n"
                                          "rules:\n"
                                          "  - DOMAIN-SUFFIX,example.com,DIRECT\n"
                                          "  - IP-CIDR,10.0.0.0/8,DIRECT\n"
                                          "  - MATCH,DIRECT\n"),
                           QStringLiteral("a.yaml"))
                .isEmpty());

    const QVector<Diagnostic> diagnostics =
        yaml->diagnose(QStringLiteral("proxies:\n  - name: [unclosed\n"), QStringLiteral("a.yaml"));
    QCOMPARE(diagnostics.size(), 1);
    QCOMPARE(diagnostics.first().severity, DiagnosticSeverity::Error);
    QVERIFY(diagnostics.first().line >= 0);
    QVERIFY(diagnostics.first().message.contains(QStringLiteral("YAML")));
}

void LanguageServicesTest::yamlReportsDuplicateKeys()
{
    LanguageProvider* yaml = LanguageRegistry::instance().providerById(QStringLiteral("yaml"));
    const QVector<Diagnostic> diagnostics =
        yaml->diagnose(QStringLiteral("mixed-port: 7890\nmixed-port: 7891\n"),
                       QStringLiteral("a.yaml"));
    QVERIFY(!diagnostics.isEmpty());
    QVERIFY(diagnostics.first().message.contains(QStringLiteral("重复")));
}

void LanguageServicesTest::yamlCompletesTopLevelAndSections()
{
    LanguageProvider* yaml = LanguageRegistry::instance().providerById(QStringLiteral("yaml"));
    const QStringList top = labels(yaml->completions(requestAtEnd(QString())));
    QVERIFY(top.contains(QStringLiteral("mixed-port")));
    QVERIFY(top.contains(QStringLiteral("proxies")));
    QVERIFY(top.contains(QStringLiteral("rules")));
    QVERIFY(top.contains(QStringLiteral("dns")));

    const QStringList filtered = labels(yaml->completions(requestAtEnd(QStringLiteral("mix"))));
    QVERIFY(filtered.contains(QStringLiteral("mixed-port")));
    QVERIFY(!filtered.contains(QStringLiteral("rules")));

    const QStringList dns =
        labels(yaml->completions(requestAtEnd(QStringLiteral("dns:\n  "))));
    QVERIFY(dns.contains(QStringLiteral("nameserver")));
    QVERIFY(dns.contains(QStringLiteral("enhanced-mode")));
    QVERIFY(!dns.contains(QStringLiteral("mixed-port")));
}

void LanguageServicesTest::yamlCompletesValuesAndRules()
{
    LanguageProvider* yaml = LanguageRegistry::instance().providerById(QStringLiteral("yaml"));
    const QStringList modes =
        labels(yaml->completions(requestAtEnd(QStringLiteral("mode: "))));
    QVERIFY(modes.contains(QStringLiteral("rule")));
    QVERIFY(modes.contains(QStringLiteral("global")));
    QVERIFY(modes.contains(QStringLiteral("direct")));

    const QStringList rules =
        labels(yaml->completions(requestAtEnd(QStringLiteral("rules:\n  - "))));
    QVERIFY(rules.contains(QStringLiteral("DOMAIN-SUFFIX,")));
    QVERIFY(rules.contains(QStringLiteral("MATCH,")));
}

void LanguageServicesTest::javascriptReportsSyntaxErrors()
{
    LanguageProvider* javascript =
        LanguageRegistry::instance().providerById(QStringLiteral("javascript"));
    QVERIFY(javascript);
    QVERIFY(javascript->diagnose(QStringLiteral("function main(config) { return config; }\n"),
                                 QStringLiteral("a.js"))
                .isEmpty());

    const QVector<Diagnostic> diagnostics =
        javascript->diagnose(QStringLiteral("function main( {\n"), QStringLiteral("a.js"));
    QVERIFY(!diagnostics.isEmpty());
    QCOMPARE(diagnostics.first().severity, DiagnosticSeverity::Error);
    QVERIFY(diagnostics.first().line >= 0);
}

void LanguageServicesTest::javascriptWarnsMissingMain()
{
    LanguageProvider* javascript =
        LanguageRegistry::instance().providerById(QStringLiteral("javascript"));
    const QVector<Diagnostic> diagnostics =
        javascript->diagnose(QStringLiteral("const value = 1;\n"), QStringLiteral("a.js"));
    QVERIFY(!diagnostics.isEmpty());
    QCOMPARE(diagnostics.first().severity, DiagnosticSeverity::Warning);
    QVERIFY(diagnostics.first().message.contains(QStringLiteral("main")));
}

void LanguageServicesTest::javascriptCompletesKeywordsAndConfig()
{
    LanguageProvider* javascript =
        LanguageRegistry::instance().providerById(QStringLiteral("javascript"));
    const QStringList general = labels(javascript->completions(requestAtEnd(QString())));
    QVERIFY(general.contains(QStringLiteral("function")));
    QVERIFY(general.contains(QStringLiteral("main")));

    const QStringList console =
        labels(javascript->completions(requestAtEnd(QStringLiteral("console."))));
    QVERIFY(console.contains(QStringLiteral("log")));
    QVERIFY(console.contains(QStringLiteral("error")));

    const QStringList config =
        labels(javascript->completions(requestAtEnd(QStringLiteral("config."))));
    QVERIFY(config.contains(QStringLiteral("proxies")));
    QVERIFY(config.contains(QStringLiteral("dns")));

    const QStringList dns = labels(
        javascript->completions(requestAtEnd(QStringLiteral("config.dns."))));
    QVERIFY(dns.contains(QStringLiteral("nameserver")));
    QVERIFY(!dns.contains(QStringLiteral("enhanced-mode")));

    const QStringList dnsBracket = labels(
        javascript->completions(requestAtEnd(QStringLiteral("config.dns[\""))));
    QVERIFY(dnsBracket.contains(QStringLiteral("enhanced-mode")));
    QVERIFY(dnsBracket.contains(QStringLiteral("fake-ip-range")));
}

QTEST_GUILESS_MAIN(LanguageServicesTest)

#include "test_language_services.moc"
