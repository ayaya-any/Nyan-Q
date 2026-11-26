#include "lang/LanguageRegistry.h"

#include <QFile>
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
    void yamlReportsInvalidTunRoutePrefixes();
    void yamlCompletesTopLevelAndSections();
    void yamlCompletesValuesAndRules();
    void javascriptReportsSyntaxErrors();
    void javascriptWarnsMissingMain();
    void javascriptCompletesKeywordsAndConfig();
    void yamlFormatsIndentAndComments();
    void yamlFormattingShiftsBlockScalarContent();
    void yamlFormatsAnchorsAndFlowStyle();
    void yamlFormatsStarterConfig();
    void yamlFormatterRejectsInvalidSyntax();
    void javascriptFormatsIndentation();
    void javascriptFormatterPreservesTemplatesAndComments();
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
    LanguageProvider* yaml = registry.providerById(QStringLiteral("yaml"));
    LanguageProvider* javascript = registry.providerById(QStringLiteral("javascript"));
    QVERIFY(yaml && yaml->supportsFormatting());
    QVERIFY(javascript && javascript->supportsFormatting());
    FormatResult formatted = yaml->format(QStringLiteral("a:  1\n"), QStringLiteral("a.yaml"));
    QVERIFY2(formatted.ok, qPrintable(formatted.error));
    QCOMPARE(formatted.text, QStringLiteral("a:  1\n"));
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

void LanguageServicesTest::yamlReportsInvalidTunRoutePrefixes()
{
    LanguageProvider* yaml = LanguageRegistry::instance().providerById(QStringLiteral("yaml"));
    QVERIFY(yaml);

    QVERIFY(yaml->diagnose(QStringLiteral("tun:\n"
                                          "  route-exclude-address:\n"
                                          "    - 192.168.0.0/16\n"
                                          "    - fc00::/7\n"),
                           QStringLiteral("a.yaml"))
                .isEmpty());

    const QVector<Diagnostic> diagnostics = yaml->diagnose(
        QStringLiteral("tun:\n"
                       "  route-exclude-address:\n"
                       "    - not-a-cidr\n"
                       "    - 10.0.0.0/33\n"
                       "    - 192.168.0.0/16\n"),
        QStringLiteral("a.yaml"));
    QCOMPARE(diagnostics.size(), 2);
    for (const Diagnostic& diagnostic : diagnostics) {
        QCOMPARE(diagnostic.severity, DiagnosticSeverity::Warning);
        QVERIFY(diagnostic.message.contains(QStringLiteral("IP/CIDR")));
    }

    const QVector<Diagnostic> scalar = yaml->diagnose(
        QStringLiteral("tun:\n  route-exclude-address: 192.168.0.0/16\n"),
        QStringLiteral("a.yaml"));
    QCOMPARE(scalar.size(), 1);
    QVERIFY(scalar.first().message.contains(QStringLiteral("列表")));
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

void LanguageServicesTest::yamlFormatsIndentAndComments()
{
    LanguageProvider* yaml = LanguageRegistry::instance().providerById(QStringLiteral("yaml"));
    QVERIFY(yaml);
    const QString input = QStringLiteral(
        "mixed-port: 7890\n"
        "ports:\n"
        "    - 7890\n"
        "   # section\n"
        "proxies:\n"
        "    - name: test\n"
        "      type: ss\n"
        "      server: example.com\n"
        "rules:\n"
        "    - MATCH,DIRECT\n"
        "script: |\n"
        "  line one\n"
        "    line two\n"
        "  line three\n"
        "\n"
        "dns:\n"
        "    enable: true\n");
    const QString expected = QStringLiteral(
        "mixed-port: 7890\n"
        "ports:\n"
        "  - 7890\n"
        "# section\n"
        "proxies:\n"
        "  - name: test\n"
        "    type: ss\n"
        "    server: example.com\n"
        "rules:\n"
        "  - MATCH,DIRECT\n"
        "script: |\n"
        "  line one\n"
        "    line two\n"
        "  line three\n"
        "\n"
        "dns:\n"
        "  enable: true\n");
    FormatResult result = yaml->format(input, QStringLiteral("a.yaml"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.text, expected);
    // Formatting is idempotent.
    FormatResult again = yaml->format(result.text, QStringLiteral("a.yaml"));
    QVERIFY2(again.ok, qPrintable(again.error));
    QCOMPARE(again.text, result.text);
}

void LanguageServicesTest::yamlFormattingShiftsBlockScalarContent()
{
    LanguageProvider* yaml = LanguageRegistry::instance().providerById(QStringLiteral("yaml"));
    QVERIFY(yaml);
    const QString input = QStringLiteral(
        "script: |\n"
        "    line1\n"
        "      line2\n"
        "list:\n"
        "    - |-\n"
        "        item1\n"
        "        item2\n");
    const QString expected = QStringLiteral(
        "script: |\n"
        "    line1\n"
        "      line2\n"
        "list:\n"
        "  - |-\n"
        "      item1\n"
        "      item2\n");
    FormatResult result = yaml->format(input, QStringLiteral("a.yaml"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.text, expected);
}

void LanguageServicesTest::yamlFormatsAnchorsAndFlowStyle()
{
    LanguageProvider* yaml = LanguageRegistry::instance().providerById(QStringLiteral("yaml"));
    QVERIFY(yaml);
    const QString input = QStringLiteral(
        "defaults: &def\n"
        "    interval: 300\n"
        "    timeout: 5\n"
        "proxies:\n"
        "    - {name: a, type: ss}\n"
        "    - name: b\n"
        "      <<: *def\n"
        "      ports: [80, 443]\n"
        "      note: \"multi\n"
        "        line\"\n");
    const QString expected = QStringLiteral(
        "defaults: &def\n"
        "  interval: 300\n"
        "  timeout: 5\n"
        "proxies:\n"
        "  - {name: a, type: ss}\n"
        "  - name: b\n"
        "    <<: *def\n"
        "    ports: [80, 443]\n"
        "    note: \"multi\n"
        "      line\"\n");
    FormatResult result = yaml->format(input, QStringLiteral("a.yaml"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.text, expected);
}

void LanguageServicesTest::yamlFormatsStarterConfig()
{
    LanguageProvider* yaml = LanguageRegistry::instance().providerById(QStringLiteral("yaml"));
    QVERIFY(yaml);
    const QString path = QFINDTESTDATA("../resources/config/starter.yaml");
    QVERIFY(path.isEmpty() == false);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString input = QString::fromUtf8(file.readAll());
    file.close();
    FormatResult result = yaml->format(input, path);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.text.contains(QStringLiteral("# Nyan-Q built-in starter configuration.")));
    QVERIFY(result.text.endsWith(QLatin1Char('\n')));
    FormatResult again = yaml->format(result.text, path);
    QVERIFY2(again.ok, qPrintable(again.error));
    QCOMPARE(again.text, result.text);
}

void LanguageServicesTest::yamlFormatterRejectsInvalidSyntax()
{
    LanguageProvider* yaml = LanguageRegistry::instance().providerById(QStringLiteral("yaml"));
    QVERIFY(yaml);
    FormatResult result =
        yaml->format(QStringLiteral("proxies:\n  - name: [unclosed\n"), QStringLiteral("a.yaml"));
    QVERIFY(!result.ok);
    QVERIFY(result.text.isEmpty());
    QVERIFY2(result.error.contains(QStringLiteral("YAML")), qPrintable(result.error));
}

void LanguageServicesTest::javascriptFormatsIndentation()
{
    LanguageProvider* javascript =
        LanguageRegistry::instance().providerById(QStringLiteral("javascript"));
    QVERIFY(javascript);
    const QString input = QStringLiteral(
        "function main(config, profileName) {\n"
        "const rules = []\n"
        "if (config) {\n"
        "rules.push(\"MATCH,DIRECT\")\n"
        "}\n"
        "return config\n"
        "}\n");
    const QString expected = QStringLiteral(
        "function main(config, profileName) {\n"
        "  const rules = []\n"
        "  if (config) {\n"
        "    rules.push(\"MATCH,DIRECT\")\n"
        "  }\n"
        "  return config\n"
        "}\n");
    FormatResult result = javascript->format(input, QStringLiteral("a.js"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.text, expected);
    FormatResult again = javascript->format(result.text, QStringLiteral("a.js"));
    QVERIFY2(again.ok, qPrintable(again.error));
    QCOMPARE(again.text, result.text);
}

void LanguageServicesTest::javascriptFormatterPreservesTemplatesAndComments()
{
    LanguageProvider* javascript =
        LanguageRegistry::instance().providerById(QStringLiteral("javascript"));
    QVERIFY(javascript);
    const QString input = QStringLiteral(
        "const t = `a\n"
        "    keep   this\n"
        "b`;\n"
        "// comment { not a brace\n"
        "const s = \"{\"\n"
        "const r = /[{}]/\n"
        "if (s) {\n"
        "console.log(t, r)\n"
        "}\n");
    const QString expected = QStringLiteral(
        "const t = `a\n"
        "    keep   this\n"
        "b`;\n"
        "// comment { not a brace\n"
        "const s = \"{\"\n"
        "const r = /[{}]/\n"
        "if (s) {\n"
        "  console.log(t, r)\n"
        "}\n");
    FormatResult result = javascript->format(input, QStringLiteral("a.js"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.text, expected);
}

QTEST_GUILESS_MAIN(LanguageServicesTest)

#include "test_language_services.moc"
