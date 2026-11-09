#include "lang/YamlLanguageProvider.h"

#include "yaml.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>

namespace nq::lang {
namespace {

struct KeySpec {
    QString key;
    QString detail;
};

QVector<KeySpec> rootKeys()
{
    return {
        {QStringLiteral("mixed-port"), QStringLiteral("混合代理端口")},
        {QStringLiteral("port"), QStringLiteral("HTTP 代理端口")},
        {QStringLiteral("socks-port"), QStringLiteral("SOCKS5 代理端口")},
        {QStringLiteral("redir-port"), QStringLiteral("Redir 端口")},
        {QStringLiteral("tproxy-port"), QStringLiteral("TProxy 端口")},
        {QStringLiteral("allow-lan"), QStringLiteral("允许局域网连接")},
        {QStringLiteral("bind-address"), QStringLiteral("监听地址")},
        {QStringLiteral("mode"), QStringLiteral("rule / global / direct")},
        {QStringLiteral("log-level"), QStringLiteral("日志等级")},
        {QStringLiteral("ipv6"), QStringLiteral("启用 IPv6")},
        {QStringLiteral("external-controller"), QStringLiteral("REST API 监听地址")},
        {QStringLiteral("secret"), QStringLiteral("REST API 密钥")},
        {QStringLiteral("unified-delay"), QStringLiteral("统一延迟计算")},
        {QStringLiteral("tcp-concurrent"), QStringLiteral("TCP 并发")},
        {QStringLiteral("find-process-mode"), QStringLiteral("进程查找模式")},
        {QStringLiteral("geodata-mode"), QStringLiteral("Geodata 模式")},
        {QStringLiteral("geo-auto-update"), QStringLiteral("Geodata 自动更新")},
        {QStringLiteral("geo-update-interval"), QStringLiteral("Geodata 更新间隔")},
        {QStringLiteral("global-client-fingerprint"), QStringLiteral("全局客户端指纹")},
        {QStringLiteral("interface-name"), QStringLiteral("出站网卡")},
        {QStringLiteral("routing-mark"), QStringLiteral("路由标记")},
        {QStringLiteral("proxies"), QStringLiteral("代理节点列表")},
        {QStringLiteral("proxy-groups"), QStringLiteral("代理组列表")},
        {QStringLiteral("proxy-providers"), QStringLiteral("代理提供者")},
        {QStringLiteral("rule-providers"), QStringLiteral("规则提供者")},
        {QStringLiteral("rules"), QStringLiteral("分流规则列表")},
        {QStringLiteral("hosts"), QStringLiteral("静态 Hosts")},
        {QStringLiteral("dns"), QStringLiteral("DNS 设置")},
        {QStringLiteral("tun"), QStringLiteral("TUN 设置")},
        {QStringLiteral("sniffer"), QStringLiteral("域名嗅探")},
        {QStringLiteral("profile"), QStringLiteral("缓存与选择持久化")},
        {QStringLiteral("experimental"), QStringLiteral("实验性选项")},
        {QStringLiteral("ntp"), QStringLiteral("NTP 时间同步")},
        {QStringLiteral("listeners"), QStringLiteral("额外监听器")},
        {QStringLiteral("authentication"), QStringLiteral("代理认证用户")},
    };
}

QVector<KeySpec> keysForSection(const QString& section)
{
    const QString key = section.toLower();
    if (key == QStringLiteral("dns")) {
        return {
            {QStringLiteral("enable"), QStringLiteral("启用 DNS")},
            {QStringLiteral("ipv6"), QStringLiteral("启用 IPv6 DNS")},
            {QStringLiteral("listen"), QStringLiteral("DNS 监听地址")},
            {QStringLiteral("enhanced-mode"), QStringLiteral("fake-ip / redir-host / normal")},
            {QStringLiteral("fake-ip-range"), QStringLiteral("fake-ip 地址段")},
            {QStringLiteral("fake-ip-filter"), QStringLiteral("不使用 fake-ip 的域名")},
            {QStringLiteral("default-nameserver"), QStringLiteral("解析 nameserver 的 DNS")},
            {QStringLiteral("nameserver"), QStringLiteral("默认 DNS 服务器")},
            {QStringLiteral("fallback"), QStringLiteral("fallback DNS 服务器")},
            {QStringLiteral("fallback-filter"), QStringLiteral("fallback 条件")},
            {QStringLiteral("nameserver-policy"), QStringLiteral("按域名指定 DNS")},
            {QStringLiteral("proxy-server-nameserver"), QStringLiteral("代理服务器域名 DNS")},
            {QStringLiteral("respect-rules"), QStringLiteral("DNS 请求遵循规则")},
            {QStringLiteral("use-hosts"), QStringLiteral("使用 hosts")},
            {QStringLiteral("use-system-hosts"), QStringLiteral("使用系统 hosts")},
            {QStringLiteral("prefer-h3"), QStringLiteral("优先 HTTP/3")},
            {QStringLiteral("cache-algorithm"), QStringLiteral("缓存算法")},
        };
    }
    if (key == QStringLiteral("tun")) {
        return {
            {QStringLiteral("enable"), QStringLiteral("启用 TUN")},
            {QStringLiteral("stack"), QStringLiteral("system / gvisor / mixed")},
            {QStringLiteral("device"), QStringLiteral("设备名")},
            {QStringLiteral("auto-route"), QStringLiteral("自动路由")},
            {QStringLiteral("auto-detect-interface"), QStringLiteral("自动检测网卡")},
            {QStringLiteral("dns-hijack"), QStringLiteral("DNS 劫持地址")},
            {QStringLiteral("strict-route"), QStringLiteral("严格路由")},
            {QStringLiteral("mtu"), QStringLiteral("MTU")},
            {QStringLiteral("gso"), QStringLiteral("GSO")},
            {QStringLiteral("gso-max-size"), QStringLiteral("GSO 最大尺寸")},
            {QStringLiteral("route-address"), QStringLiteral("路由地址")},
            {QStringLiteral("route-exclude-address"), QStringLiteral("排除路由地址")},
            {QStringLiteral("include-uid"), QStringLiteral("包含 UID")},
            {QStringLiteral("exclude-uid"), QStringLiteral("排除 UID")},
            {QStringLiteral("include-package"), QStringLiteral("包含应用包名")},
            {QStringLiteral("exclude-package"), QStringLiteral("排除应用包名")},
        };
    }
    if (key == QStringLiteral("sniffer")) {
        return {
            {QStringLiteral("enable"), QStringLiteral("启用域名嗅探")},
            {QStringLiteral("force-dns-mapping"), QStringLiteral("强制 DNS 映射")},
            {QStringLiteral("parse-pure-ip"), QStringLiteral("解析纯 IP 连接")},
            {QStringLiteral("override-destination"), QStringLiteral("覆盖目标地址")},
            {QStringLiteral("sniff"), QStringLiteral("嗅探协议列表")},
            {QStringLiteral("force-domain"), QStringLiteral("强制嗅探域名")},
            {QStringLiteral("skip-domain"), QStringLiteral("跳过嗅探域名")},
            {QStringLiteral("skip-src-address"), QStringLiteral("跳过源地址")},
            {QStringLiteral("skip-dst-address"), QStringLiteral("跳过目标地址")},
        };
    }
    if (key == QStringLiteral("proxy-groups")) {
        return {
            {QStringLiteral("name"), QStringLiteral("代理组名称")},
            {QStringLiteral("type"), QStringLiteral("select / url-test / fallback / load-balance / relay")},
            {QStringLiteral("proxies"), QStringLiteral("包含的节点或代理组")},
            {QStringLiteral("use"), QStringLiteral("引用的 proxy-provider")},
            {QStringLiteral("url"), QStringLiteral("测速地址")},
            {QStringLiteral("interval"), QStringLiteral("测速间隔（秒）")},
            {QStringLiteral("tolerance"), QStringLiteral("延迟容差")},
            {QStringLiteral("lazy"), QStringLiteral("懒加载")},
            {QStringLiteral("timeout"), QStringLiteral("测速超时")},
            {QStringLiteral("max-failed-times"), QStringLiteral("最大失败次数")},
            {QStringLiteral("expected-status"), QStringLiteral("期望 HTTP 状态")},
            {QStringLiteral("hidden"), QStringLiteral("在 UI 中隐藏")},
            {QStringLiteral("icon"), QStringLiteral("图标 URL")},
            {QStringLiteral("disable-udp"), QStringLiteral("禁用 UDP")},
            {QStringLiteral("filter"), QStringLiteral("节点过滤正则")},
            {QStringLiteral("strategy"), QStringLiteral("load-balance 策略")},
        };
    }
    if (key == QStringLiteral("proxies")) {
        return {
            {QStringLiteral("name"), QStringLiteral("节点名称")},
            {QStringLiteral("type"), QStringLiteral("节点协议")},
            {QStringLiteral("server"), QStringLiteral("服务器地址")},
            {QStringLiteral("port"), QStringLiteral("服务器端口")},
            {QStringLiteral("cipher"), QStringLiteral("加密方式")},
            {QStringLiteral("password"), QStringLiteral("密码")},
            {QStringLiteral("uuid"), QStringLiteral("UUID")},
            {QStringLiteral("alterId"), QStringLiteral("alterId")},
            {QStringLiteral("flow"), QStringLiteral("XTLS flow")},
            {QStringLiteral("tls"), QStringLiteral("启用 TLS")},
            {QStringLiteral("servername"), QStringLiteral("TLS 域名")},
            {QStringLiteral("sni"), QStringLiteral("SNI")},
            {QStringLiteral("alpn"), QStringLiteral("ALPN")},
            {QStringLiteral("skip-cert-verify"), QStringLiteral("跳过证书校验")},
            {QStringLiteral("fingerprint"), QStringLiteral("证书指纹")},
            {QStringLiteral("client-fingerprint"), QStringLiteral("客户端指纹")},
            {QStringLiteral("network"), QStringLiteral("传输网络")},
            {QStringLiteral("ws-opts"), QStringLiteral("WebSocket 选项")},
            {QStringLiteral("grpc-opts"), QStringLiteral("gRPC 选项")},
            {QStringLiteral("reality-opts"), QStringLiteral("REALITY 选项")},
            {QStringLiteral("plugin"), QStringLiteral("插件")},
            {QStringLiteral("plugin-opts"), QStringLiteral("插件选项")},
            {QStringLiteral("udp"), QStringLiteral("启用 UDP")},
            {QStringLiteral("up"), QStringLiteral("上行带宽")},
            {QStringLiteral("down"), QStringLiteral("下行带宽")},
            {QStringLiteral("dialer-proxy"), QStringLiteral("前置代理")},
            {QStringLiteral("interface-name"), QStringLiteral("出站网卡")},
            {QStringLiteral("routing-mark"), QStringLiteral("路由标记")},
            {QStringLiteral("ip-version"), QStringLiteral("IP 版本")},
            {QStringLiteral("ports"), QStringLiteral("端口跳跃")},
            {QStringLiteral("username"), QStringLiteral("用户名")},
            {QStringLiteral("obfs"), QStringLiteral("混淆方式")},
            {QStringLiteral("obfs-password"), QStringLiteral("混淆密码")},
            {QStringLiteral("protocol"), QStringLiteral("协议")},
            {QStringLiteral("protocol-param"), QStringLiteral("协议参数")},
            {QStringLiteral("smux"), QStringLiteral("SMUX 选项")},
        };
    }
    if (key == QStringLiteral("rule-providers") ||
        key == QStringLiteral("proxy-providers")) {
        return {
            {QStringLiteral("type"), QStringLiteral("http / file / inline")},
            {QStringLiteral("behavior"), QStringLiteral("domain / ipcidr / classical")},
            {QStringLiteral("url"), QStringLiteral("远程地址")},
            {QStringLiteral("path"), QStringLiteral("本地缓存路径")},
            {QStringLiteral("interval"), QStringLiteral("更新间隔（秒）")},
            {QStringLiteral("proxy"), QStringLiteral("下载代理")},
            {QStringLiteral("size-limit"), QStringLiteral("大小限制")},
            {QStringLiteral("header"), QStringLiteral("HTTP 请求头")},
            {QStringLiteral("health-check"), QStringLiteral("健康检查")},
            {QStringLiteral("lazy"), QStringLiteral("懒加载")},
            {QStringLiteral("format"), QStringLiteral("payload 格式")},
            {QStringLiteral("payload"), QStringLiteral("内联内容")},
        };
    }
    if (key == QStringLiteral("profile")) {
        return {
            {QStringLiteral("store-selected"), QStringLiteral("保存手动选择的节点")},
            {QStringLiteral("store-fake-ip"), QStringLiteral("保存 fake-ip 映射")},
            {QStringLiteral("tracing"), QStringLiteral("追踪")},
        };
    }
    if (key == QStringLiteral("experimental")) {
        return {
            {QStringLiteral("sniff-tls-sni"), QStringLiteral("嗅探 TLS SNI")},
            {QStringLiteral("quic-go-disable-gso"), QStringLiteral("禁用 QUIC GSO")},
            {QStringLiteral("dialer-ip4p-convert"), QStringLiteral("IP4P 转换")},
        };
    }
    return {};
}

QStringList valueOptions(const QString& section, const QString& key)
{
    const QString s = section.toLower();
    const QString k = key.toLower();
    if (s.isEmpty()) {
        if (k == QStringLiteral("mode"))
            return {QStringLiteral("rule"), QStringLiteral("global"), QStringLiteral("direct")};
        if (k == QStringLiteral("log-level"))
            return {QStringLiteral("info"), QStringLiteral("warning"), QStringLiteral("debug"),
                    QStringLiteral("error"), QStringLiteral("silent")};
        if (k == QStringLiteral("find-process-mode"))
            return {QStringLiteral("always"), QStringLiteral("strict"), QStringLiteral("off")};
        if (k == QStringLiteral("allow-lan") || k == QStringLiteral("ipv6") ||
            k == QStringLiteral("unified-delay") || k == QStringLiteral("tcp-concurrent") ||
            k == QStringLiteral("geodata-mode") || k == QStringLiteral("geo-auto-update"))
            return {QStringLiteral("true"), QStringLiteral("false")};
    }
    if (s == QStringLiteral("dns")) {
        if (k == QStringLiteral("enhanced-mode"))
            return {QStringLiteral("fake-ip"), QStringLiteral("redir-host"),
                    QStringLiteral("normal")};
        if (k == QStringLiteral("enable") || k == QStringLiteral("ipv6") ||
            k == QStringLiteral("respect-rules") || k == QStringLiteral("use-hosts") ||
            k == QStringLiteral("use-system-hosts") || k == QStringLiteral("prefer-h3"))
            return {QStringLiteral("true"), QStringLiteral("false")};
    }
    if (s == QStringLiteral("tun")) {
        if (k == QStringLiteral("stack"))
            return {QStringLiteral("system"), QStringLiteral("gvisor"), QStringLiteral("mixed")};
        if (k == QStringLiteral("enable") || k == QStringLiteral("auto-route") ||
            k == QStringLiteral("auto-detect-interface") || k == QStringLiteral("strict-route") ||
            k == QStringLiteral("gso"))
            return {QStringLiteral("true"), QStringLiteral("false")};
    }
    if (s == QStringLiteral("sniffer")) {
        if (k == QStringLiteral("enable") || k == QStringLiteral("force-dns-mapping") ||
            k == QStringLiteral("parse-pure-ip") || k == QStringLiteral("override-destination"))
            return {QStringLiteral("true"), QStringLiteral("false")};
    }
    if (s == QStringLiteral("proxy-groups")) {
        if (k == QStringLiteral("type"))
            return {QStringLiteral("select"), QStringLiteral("url-test"),
                    QStringLiteral("fallback"), QStringLiteral("load-balance"),
                    QStringLiteral("relay")};
        if (k == QStringLiteral("lazy") || k == QStringLiteral("hidden") ||
            k == QStringLiteral("disable-udp"))
            return {QStringLiteral("true"), QStringLiteral("false")};
    }
    if (s == QStringLiteral("proxies")) {
        if (k == QStringLiteral("type"))
            return {QStringLiteral("ss"), QStringLiteral("vmess"), QStringLiteral("vless"),
                    QStringLiteral("trojan"), QStringLiteral("hysteria"),
                    QStringLiteral("hysteria2"), QStringLiteral("tuic"), QStringLiteral("snell"),
                    QStringLiteral("socks5"), QStringLiteral("http"), QStringLiteral("wireguard"),
                    QStringLiteral("anytls"), QStringLiteral("direct")};
        if (k == QStringLiteral("tls") || k == QStringLiteral("udp") ||
            k == QStringLiteral("skip-cert-verify"))
            return {QStringLiteral("true"), QStringLiteral("false")};
        if (k == QStringLiteral("network"))
            return {QStringLiteral("tcp"), QStringLiteral("ws"), QStringLiteral("grpc"),
                    QStringLiteral("h2"), QStringLiteral("http")};
    }
    if (s == QStringLiteral("rule-providers") || s == QStringLiteral("proxy-providers")) {
        if (k == QStringLiteral("type"))
            return {QStringLiteral("http"), QStringLiteral("file"), QStringLiteral("inline")};
        if (k == QStringLiteral("behavior"))
            return {QStringLiteral("domain"), QStringLiteral("ipcidr"),
                    QStringLiteral("classical")};
        if (k == QStringLiteral("lazy"))
            return {QStringLiteral("true"), QStringLiteral("false")};
    }
    if (s == QStringLiteral("profile") && k == QStringLiteral("store-selected"))
        return {QStringLiteral("true"), QStringLiteral("false")};
    return {};
}

QString lineText(const QString& text, int lineStart, int lineEnd)
{
    return text.mid(lineStart, lineEnd - lineStart);
}

int lineStartFor(const QString& text, int position)
{
    return text.lastIndexOf(QLatin1Char('\n'), qMax(0, position - 1)) + 1;
}

int lineEndFor(const QString& text, int position)
{
    const int end = text.indexOf(QLatin1Char('\n'), position);
    return end < 0 ? text.size() : end;
}

int indentationOf(const QString& line)
{
    int indent = 0;
    for (const QChar ch : line) {
        if (ch == QLatin1Char(' '))
            ++indent;
        else if (ch == QLatin1Char('\t'))
            indent += 2;
        else
            break;
    }
    return indent;
}

QString topLevelSection(const QString& text, int beforeLineStart)
{
    static const QRegularExpression topKey(
        QStringLiteral("^([A-Za-z0-9_.-]+)\\s*:"));
    QString section;
    const QStringList lines = text.left(beforeLineStart).split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (line.isEmpty() || line.startsWith(QLatin1Char(' ')) ||
            line.startsWith(QLatin1Char('\t')) || line.startsWith(QLatin1Char('#')))
            continue;
        const auto match = topKey.match(line);
        if (match.hasMatch())
            section = match.captured(1);
    }
    return section;
}

QStringList namesInSection(const QString& text, int beforeLineStart, const QString& section)
{
    QStringList names;
    const QStringList lines = text.left(beforeLineStart).split(QLatin1Char('\n'));
    bool inSection = false;
    static const QRegularExpression namePattern(
        QStringLiteral("^\\s*(?:-\\s+)?name\\s*:\\s*[\"']?([^\"'\\s].*?)[\"']?\\s*$"));
    for (const QString& line : lines) {
        if (!line.isEmpty() && !line.startsWith(QLatin1Char(' ')) &&
            !line.startsWith(QLatin1Char('\t')) && !line.startsWith(QLatin1Char('#'))) {
            inSection = line.startsWith(section + QLatin1Char(':'));
            continue;
        }
        if (!inSection)
            continue;
        const auto match = namePattern.match(line);
        if (match.hasMatch()) {
            const QString name = match.captured(1).trimmed();
            if (!name.isEmpty())
                names.append(name);
        }
    }
    names.removeDuplicates();
    return names;
}

QVector<CompletionItem> makeKeyItems(const QVector<KeySpec>& keys, const QString& prefix,
                                     const QString& section)
{
    QVector<CompletionItem> items;
    for (const KeySpec& spec : keys) {
        if (!prefix.isEmpty() && !spec.key.startsWith(prefix, Qt::CaseInsensitive))
            continue;
        CompletionItem item;
        item.label = spec.key;
        item.insertText = spec.key + QStringLiteral(": ");
        item.detail = spec.detail;
        item.kind = CompletionKind::Property;
        item.priority = 20;
        Q_UNUSED(section)
        items.append(item);
    }
    return items;
}

QVector<CompletionItem> makeValueItems(const QStringList& values, const QString& prefix)
{
    QVector<CompletionItem> items;
    for (const QString& value : values) {
        if (!prefix.isEmpty() && !value.startsWith(prefix, Qt::CaseInsensitive))
            continue;
        CompletionItem item;
        item.label = value;
        item.insertText = value;
        item.detail = QStringLiteral("可选值");
        item.kind = CompletionKind::Value;
        item.priority = 10;
        items.append(item);
    }
    return items;
}

class YamlHighlighter final : public QSyntaxHighlighter {
public:
    YamlHighlighter(QTextDocument* document, const LanguageTheme& theme)
        : QSyntaxHighlighter(document)
    {
        m_key.setForeground(theme.keyword);
        m_key.setFontWeight(QFont::DemiBold);
        m_string.setForeground(theme.string);
        m_number.setForeground(theme.number);
        m_keyword.setForeground(theme.keyword);
        m_punctuation.setForeground(theme.punctuation);
        m_comment.setForeground(theme.comment);
        m_comment.setFontItalic(true);
    }

protected:
    void highlightBlock(const QString& text) override
    {
        static const QRegularExpression keyPattern(
            QStringLiteral("^(\\s*(?:-\\s+)?)([A-Za-z0-9_.-]+)(\\s*:)"));
        static const QRegularExpression stringPattern(
            QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"|'(?:[^'\\\\]|\\\\.)*'"));
        static const QRegularExpression numberPattern(
            QStringLiteral("\\b-?\\d+(?:\\.\\d+)?\\b"));
        static const QRegularExpression keywordPattern(
            QStringLiteral("\\b(?:true|false|yes|no|null|on|off)\\b"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression punctuationPattern(
            QStringLiteral("^\\s*-\\s|[\\[\\]{},]"));

        const int comment = commentStart(text);
        const auto addMatches = [&](const QRegularExpression& pattern,
                                    const QTextCharFormat& format) {
            auto it = pattern.globalMatch(text);
            while (it.hasNext()) {
                const auto match = it.next();
                if (comment >= 0 && match.capturedStart() >= comment)
                    continue;
                setFormat(match.capturedStart(), match.capturedLength(), format);
            }
        };

        const auto key = keyPattern.match(text);
        if (key.hasMatch() && (comment < 0 || key.capturedStart(2) < comment))
            setFormat(key.capturedStart(2), key.capturedLength(2), m_key);

        addMatches(stringPattern, m_string);
        addMatches(numberPattern, m_number);
        addMatches(keywordPattern, m_keyword);
        addMatches(punctuationPattern, m_punctuation);
        if (comment >= 0)
            setFormat(comment, text.size() - comment, m_comment);
    }

private:
    static int commentStart(const QString& text)
    {
        bool single = false;
        bool doubleQuote = false;
        for (int i = 0; i < text.size(); ++i) {
            const QChar ch = text.at(i);
            if (ch == QLatin1Char('\\') && i + 1 < text.size()) {
                ++i;
                continue;
            }
            if (ch == QLatin1Char('\'') && !doubleQuote)
                single = !single;
            else if (ch == QLatin1Char('"') && !single)
                doubleQuote = !doubleQuote;
            else if (ch == QLatin1Char('#') && !single && !doubleQuote)
                return i;
        }
        return -1;
    }

    QTextCharFormat m_key;
    QTextCharFormat m_string;
    QTextCharFormat m_number;
    QTextCharFormat m_keyword;
    QTextCharFormat m_punctuation;
    QTextCharFormat m_comment;
};

}  // namespace

QVector<Diagnostic> YamlLanguageProvider::diagnose(const QString& text,
                                                   const QString& filePath) const
{
    Q_UNUSED(filePath)
    QVector<Diagnostic> diagnostics;
    if (text.trimmed().isEmpty())
        return diagnostics;

    QByteArray data = text.toUtf8();
    if (data.startsWith("\xEF\xBB\xBF"))
        data.remove(0, 3);

    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser))
        return diagnostics;
    yaml_parser_set_input_string(&parser, reinterpret_cast<const unsigned char*>(data.constData()),
                                 static_cast<size_t>(data.size()));

    struct Frame {
        QSet<QString> keys;
        QString pendingKey;
    };
    QVector<Frame> frames;
    QVector<bool> containers;  // true = mapping, false = sequence
    QSet<QString> anchors;
    int documents = 0;
    const auto add = [&](DiagnosticSeverity severity, int line, int column, int length,
                         const QString& message) {
        if (diagnostics.size() >= 100)
            return;
        Diagnostic diagnostic;
        diagnostic.severity = severity;
        diagnostic.line = qMax(0, line);
        diagnostic.column = qMax(0, column);
        diagnostic.length = qMax(1, length);
        diagnostic.message = message;
        diagnostic.source = QStringLiteral("YAML");
        diagnostics.append(diagnostic);
    };

    bool done = false;
    while (!done) {
        yaml_event_t event;
        if (!yaml_parser_parse(&parser, &event)) {
            add(DiagnosticSeverity::Error, static_cast<int>(parser.problem_mark.line),
                static_cast<int>(parser.problem_mark.column), 1,
                QStringLiteral("YAML 语法错误：%1")
                    .arg(QString::fromUtf8(parser.problem ? parser.problem : "无法解析 YAML")));
            break;
        }

        switch (event.type) {
        case YAML_STREAM_END_EVENT:
            done = true;
            break;
        case YAML_DOCUMENT_START_EVENT:
            ++documents;
            if (documents > 1) {
                add(DiagnosticSeverity::Warning, static_cast<int>(event.start_mark.line),
                    static_cast<int>(event.start_mark.column), 1,
                    QStringLiteral("编辑器只支持单个 YAML 文档"));
            }
            break;
        case YAML_DOCUMENT_END_EVENT:
            break;
        case YAML_MAPPING_START_EVENT: {
            if (!frames.isEmpty())
                frames.last().pendingKey.clear();
            const QString anchor = event.data.mapping_start.anchor
                                       ? QString::fromUtf8(
                                             reinterpret_cast<const char*>(
                                                 event.data.mapping_start.anchor))
                                       : QString();
            if (!anchor.isEmpty())
                anchors.insert(anchor);
            frames.append(Frame{});
            containers.append(true);
            break;
        }
        case YAML_MAPPING_END_EVENT:
            if (!frames.isEmpty())
                frames.removeLast();
            if (!containers.isEmpty())
                containers.removeLast();
            break;
        case YAML_SEQUENCE_START_EVENT: {
            if (!frames.isEmpty())
                frames.last().pendingKey.clear();
            const QString anchor = event.data.sequence_start.anchor
                                       ? QString::fromUtf8(
                                             reinterpret_cast<const char*>(
                                                 event.data.sequence_start.anchor))
                                       : QString();
            if (!anchor.isEmpty())
                anchors.insert(anchor);
            containers.append(false);
            break;
        }
        case YAML_SEQUENCE_END_EVENT:
            if (!containers.isEmpty())
                containers.removeLast();
            break;
        case YAML_SCALAR_EVENT: {
            const QString value = QString::fromUtf8(
                reinterpret_cast<const char*>(event.data.scalar.value),
                static_cast<int>(event.data.scalar.length));
            const QString anchor = event.data.scalar.anchor
                                       ? QString::fromUtf8(
                                             reinterpret_cast<const char*>(
                                                 event.data.scalar.anchor))
                                       : QString();
            if (!anchor.isEmpty())
                anchors.insert(anchor);
            if (!containers.isEmpty() && containers.last() && !frames.isEmpty()) {
                Frame& frame = frames.last();
                if (frame.pendingKey.isEmpty()) {
                    if (frame.keys.contains(value)) {
                        add(DiagnosticSeverity::Warning,
                            static_cast<int>(event.start_mark.line),
                            static_cast<int>(event.start_mark.column),
                            qMax(1, value.size()),
                            QStringLiteral("映射键“%1”重复出现").arg(value));
                    } else {
                        frame.keys.insert(value);
                    }
                    frame.pendingKey = value;
                } else {
                    frame.pendingKey.clear();
                }
            }
            break;
        }
        case YAML_ALIAS_EVENT: {
            const QString anchor = event.data.alias.anchor
                                       ? QString::fromUtf8(
                                             reinterpret_cast<const char*>(
                                                 event.data.alias.anchor))
                                       : QString();
            if (!anchor.isEmpty() && !anchors.contains(anchor)) {
                add(DiagnosticSeverity::Error, static_cast<int>(event.start_mark.line),
                    static_cast<int>(event.start_mark.column), qMax(1, anchor.size()),
                    QStringLiteral("YAML 别名引用了未定义的锚点 &%1").arg(anchor));
            }
            if (!containers.isEmpty() && containers.last() && !frames.isEmpty())
                frames.last().pendingKey.clear();
            break;
        }
        default:
            break;
        }
        yaml_event_delete(&event);
        if (diagnostics.size() >= 100)
            break;
    }
    yaml_parser_delete(&parser);

    std::stable_sort(diagnostics.begin(), diagnostics.end(),
                     [](const Diagnostic& a, const Diagnostic& b) {
                         if (a.line != b.line)
                             return a.line < b.line;
                         return a.column < b.column;
                     });
    return diagnostics;
}

QVector<CompletionItem> YamlLanguageProvider::completions(
    const CompletionRequest& request) const
{
    const QString& text = request.text;
    const int position = qBound(0, request.position, text.size());
    const int start = lineStartFor(text, position);
    const int end = lineEndFor(text, position);
    const QString line = lineText(text, start, end);
    const int cursorColumn = position - start;
    const QString before = line.left(cursorColumn);
    const QString trimmedBefore = before.trimmed();

    if (trimmedBefore.startsWith(QLatin1Char('#')))
        return {};
    // Do not offer structural completions inside an unfinished quoted scalar.
    int quotes = 0;
    for (int i = 0; i < before.size(); ++i) {
        if (before.at(i) == QLatin1Char('"') &&
            (i == 0 || before.at(i - 1) != QLatin1Char('\\')))
            ++quotes;
    }
    if (quotes % 2 == 1)
        return {};

    const int indent = indentationOf(line);
    const QString section = indent == 0 ? QString() : topLevelSection(text, start);

    static const QRegularExpression valuePattern(
        QStringLiteral("^\\s*(?:-\\s*)?([A-Za-z0-9_.-]+)\\s*:\\s*(.*)$"));
    const auto valueMatch = valuePattern.match(before);
    if (valueMatch.hasMatch()) {
        const QString key = valueMatch.captured(1);
        QString valuePrefix = valueMatch.captured(2).trimmed();
        if (valuePrefix.startsWith(QLatin1Char('\'')) ||
            valuePrefix.startsWith(QLatin1Char('"')))
            valuePrefix.remove(0, 1);

        if (key.compare(QStringLiteral("proxies"), Qt::CaseInsensitive) == 0 &&
            (section.compare(QStringLiteral("proxy-groups"), Qt::CaseInsensitive) == 0)) {
            QStringList names = namesInSection(text, start, QStringLiteral("proxies"));
            names.append(namesInSection(text, start, QStringLiteral("proxy-groups")));
            names.append(QStringLiteral("DIRECT"));
            names.append(QStringLiteral("REJECT"));
            names.removeDuplicates();
            return makeValueItems(names, valuePrefix);
        }
        if (key.compare(QStringLiteral("use"), Qt::CaseInsensitive) == 0 &&
            section.compare(QStringLiteral("proxy-groups"), Qt::CaseInsensitive) == 0) {
            return makeValueItems(namesInSection(text, start, QStringLiteral("proxy-providers")),
                                  valuePrefix);
        }
        return makeValueItems(valueOptions(section, key), valuePrefix);
    }

    static const QRegularExpression wordPattern(QStringLiteral("([A-Za-z0-9_.-]*)$"));
    const auto wordMatch = wordPattern.match(before);
    const QString prefix = wordMatch.hasMatch() ? wordMatch.captured(1) : QString();

    if (section.compare(QStringLiteral("rules"), Qt::CaseInsensitive) == 0 &&
        !before.contains(QLatin1Char(':'))) {
        const QStringList rules = {
            QStringLiteral("DOMAIN-SUFFIX,"), QStringLiteral("DOMAIN,"),
            QStringLiteral("DOMAIN-KEYWORD,"), QStringLiteral("IP-CIDR,"),
            QStringLiteral("IP-CIDR6,"),     QStringLiteral("GEOIP,"),
            QStringLiteral("GEOSITE,"),     QStringLiteral("RULE-SET,"),
            QStringLiteral("PROCESS-NAME,"), QStringLiteral("MATCH,"),
        };
        QVector<CompletionItem> items;
        for (const QString& rule : rules) {
            if (!prefix.isEmpty() && !rule.startsWith(prefix, Qt::CaseInsensitive))
                continue;
            CompletionItem item;
            item.label = rule;
            item.insertText = rule;
            item.detail = QStringLiteral("Mihomo 规则");
            item.kind = CompletionKind::Snippet;
            item.priority = 5;
            items.append(item);
        }
        return items;
    }

    if (before.contains(QLatin1Char(':')))
        return {};

    QVector<KeySpec> keys;
    if (indent == 0)
        keys = rootKeys();
    else
        keys = keysForSection(section);
    if (keys.isEmpty())
        return makeKeyItems(rootKeys(), prefix, section);

    return makeKeyItems(keys, prefix, section);
}

QSyntaxHighlighter* YamlLanguageProvider::createHighlighter(
    QTextDocument* document, const LanguageTheme& theme) const
{
    return new YamlHighlighter(document, theme);
}

}  // namespace nq::lang
