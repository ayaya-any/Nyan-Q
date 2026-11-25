#include "core/RuntimeConfig.h"

#include "scripts/YamlDocument.h"

#include <QAbstractSocket>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QSaveFile>
#include <QStringList>
#include <QTextStream>

namespace {

constexpr char kManagedHeader[] = "# ---- Nyan-Q managed runtime settings ----";

QString quoteYaml(const QString& value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(escaped);
}

bool isTopLevelKeyLine(const QString& line)
{
    if (line.isEmpty())
        return false;
    const QChar first = line.at(0);
    if (first == QLatin1Char(' ') || first == QLatin1Char('\t') ||
        first == QLatin1Char('#'))
        return false;
    if (line.startsWith(QStringLiteral("---")) || line.startsWith(QStringLiteral("...")))
        return false;
    return true;
}

// Returns the key of a top-level mapping line, supporting quoted keys and
// spaces before the colon ("tun :"). `ok` is false when the line is not a
// top-level key.
QString topLevelKey(const QString& line, bool* ok)
{
    *ok = false;
    if (!isTopLevelKeyLine(line))
        return {};

    if (line.at(0) == QLatin1Char('\'') || line.at(0) == QLatin1Char('"')) {
        const QChar quote = line.at(0);
        QString key;
        int pos = 1;
        bool closed = false;
        while (pos < line.size()) {
            const QChar ch = line.at(pos);
            if (ch == quote) {
                if (quote == QLatin1Char('\'') && pos + 1 < line.size() &&
                    line.at(pos + 1) == QLatin1Char('\'')) {
                    key.append(QLatin1Char('\''));
                    pos += 2;
                    continue;
                }
                closed = true;
                ++pos;
                break;
            }
            key.append(ch);
            ++pos;
        }
        if (!closed || key.isEmpty())
            return {};
        while (pos < line.size() && line.at(pos) == QLatin1Char(' '))
            ++pos;
        if (pos >= line.size() || line.at(pos) != QLatin1Char(':'))
            return {};
        *ok = true;
        return key;
    }

    const int colon = line.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return {};
    const QString key = line.left(colon).trimmed();
    if (key.isEmpty() || key.contains(QLatin1Char(' ')) || key.contains(QLatin1Char('\t')))
        return {};
    *ok = true;
    return key;
}

bool hasKey(const QStringList& lines, const QString& key)
{
    for (const QString& line : lines) {
        bool ok = false;
        if (topLevelKey(line, &ok) == key && ok)
            return true;
    }
    return false;
}

void removeKeys(QStringList& lines, const QStringList& keys)
{
    QStringList output;
    output.reserve(lines.size());
    bool skipping = false;

    for (const QString& line : lines) {
        if (skipping) {
            if (line.isEmpty() || line.startsWith(QLatin1Char(' ')) ||
                line.startsWith(QLatin1Char('\t'))) {
                continue;
            }
            skipping = false;
        }

        bool ok = false;
        const QString key = topLevelKey(line, &ok);
        if (ok && keys.contains(key)) {
            skipping = true;
            continue;
        }
        output.append(line);
    }

    while (!output.isEmpty() && output.last().trimmed().isEmpty())
        output.removeLast();
    lines = output;
}

QStringList dnsBlock()
{
    return {
        QStringLiteral("dns:"),
        QStringLiteral("  enable: true"),
        QStringLiteral("  ipv6: false"),
        QStringLiteral("  listen: 127.0.0.1:1053"),
        QStringLiteral("  enhanced-mode: fake-ip"),
        QStringLiteral("  fake-ip-range: 198.18.0.1/16"),
        QStringLiteral("  fake-ip-filter:"),
        QStringLiteral("    - \"*.lan\""),
        QStringLiteral("    - \"*.local\""),
        QStringLiteral("    - \"+.msftconnecttest.com\""),
        QStringLiteral("    - \"+.msftncsi.com\""),
        QStringLiteral("  default-nameserver:"),
        QStringLiteral("    - 223.5.5.5"),
        QStringLiteral("    - 1.1.1.1"),
        QStringLiteral("  nameserver:"),
        QStringLiteral("    - https://doh.pub/dns-query"),
        QStringLiteral("    - https://dns.alidns.com/dns-query"),
        QStringLiteral("  proxy-server-nameserver:"),
        QStringLiteral("    - https://doh.pub/dns-query"),
        QStringLiteral("    - https://dns.alidns.com/dns-query"),
    };
}

bool isValidPrefix(const QString& text, QString* normalized)
{
    const QString trimmed = text.trimmed();
    const int slash = trimmed.indexOf(QLatin1Char('/'));
    if (slash <= 0 || trimmed.indexOf(QLatin1Char('/'), slash + 1) >= 0)
        return false;

    const QString addressText = trimmed.left(slash);
    const QString bitsText = trimmed.mid(slash + 1);
    if (addressText.isEmpty() || bitsText.isEmpty())
        return false;
    for (const QChar ch : bitsText) {
        if (!ch.isDigit())
            return false;
    }

    QHostAddress address;
    if (!address.setAddress(addressText))
        return false;

    bool ok = false;
    const int bits = bitsText.toInt(&ok);
    if (!ok)
        return false;
    const int maxBits =
        address.protocol() == QAbstractSocket::IPv4Protocol ? 32 : 128;
    if (bits < 0 || bits > maxBits)
        return false;

    if (normalized)
        *normalized = trimmed;
    return true;
}

nq::yaml::Node stringSequence(const QStringList& values)
{
    nq::yaml::Node node = nq::yaml::Node::array();
    node.sequence.reserve(values.size());
    for (const QString& value : values)
        node.sequence.append(nq::yaml::Node::string(value));
    return node;
}

QStringList collectPrefixes(const nq::yaml::Node& tun, const QStringList& keys,
                            const QString& canonicalKey, QString* error,
                            QStringList* warnings)
{
    QStringList values;
    bool mergedDeprecated = false;

    for (const QString& key : keys) {
        const nq::yaml::Node* node = tun.find(key);
        if (!node || node->isNull())
            continue;
        if (!node->isSequence()) {
            *error = QStringLiteral("tun.%1 必须是 IP/CIDR 列表").arg(key);
            return {};
        }
        for (const nq::yaml::Node& item : node->sequence) {
            if (!item.isScalar()) {
                *error = QStringLiteral("tun.%1 的列表项必须是字符串").arg(key);
                return {};
            }
            QString normalized;
            if (!isValidPrefix(item.value, &normalized)) {
                *error = QStringLiteral("tun.%1 包含无效的 IP/CIDR：%2")
                             .arg(key, item.value.trimmed());
                return {};
            }
            if (!values.contains(normalized))
                values.append(normalized);
        }
        if (key != canonicalKey)
            mergedDeprecated = true;
    }

    if (mergedDeprecated) {
        warnings->append(QStringLiteral("已将旧写法的 inet4/inet6 路由键合并到 tun.%1")
                             .arg(canonicalKey));
    }
    return values;
}

// Builds the managed TUN section. The profile may tune the stack and the
// routing details; Nyan-Q owns enable/auto-route/auto-detect-interface and
// rebuilds dns-hijack so DNS is always intercepted once TUN is on.
bool buildTunSection(const nq::yaml::Node* source, QString* text, QString* error,
                     QStringList* warnings)
{
    nq::yaml::Node tun = nq::yaml::Node::object();
    if (source) {
        if (source->isMapping()) {
            tun = *source;
        } else if (!source->isNull()) {
            warnings->append(
                QStringLiteral("订阅中的 tun 不是映射，已使用 Nyan-Q 托管设置"));
        }
    }

    const QStringList excludes =
        collectPrefixes(tun,
                        {QStringLiteral("route-exclude-address"),
                         QStringLiteral("inet4-route-exclude-address"),
                         QStringLiteral("inet6-route-exclude-address")},
                        QStringLiteral("route-exclude-address"), error, warnings);
    if (!error->isEmpty())
        return false;

    const QStringList routes =
        collectPrefixes(tun,
                        {QStringLiteral("route-address"),
                         QStringLiteral("inet4-route-address"),
                         QStringLiteral("inet6-route-address")},
                        QStringLiteral("route-address"), error, warnings);
    if (!error->isEmpty())
        return false;

    QStringList hijack = {QStringLiteral("any:53"), QStringLiteral("tcp://any:53")};
    if (const nq::yaml::Node* value = tun.find(QStringLiteral("dns-hijack"))) {
        if (value->isSequence()) {
            for (const nq::yaml::Node& item : value->sequence) {
                if (!item.isScalar())
                    continue;
                const QString entry = item.value.trimmed();
                if (!entry.isEmpty() && !hijack.contains(entry))
                    hijack.append(entry);
            }
        } else if (!value->isNull()) {
            warnings->append(
                QStringLiteral("tun.dns-hijack 不是列表，已使用默认劫持地址"));
        }
    }

    QString stack = QStringLiteral("mips");
    if (const nq::yaml::Node* value = tun.find(QStringLiteral("stack"))) {
        if (value->isScalar() && !value->value.trimmed().isEmpty()) {
            stack = value->value.trimmed();
        } else if (!value->isNull()) {
            warnings->append(
                QStringLiteral("tun.stack 不是标量，已使用默认值 mips"));
        }
    }

    nq::yaml::Node managed = nq::yaml::Node::object();
    managed.set(QStringLiteral("enable"), nq::yaml::Node::boolean(true));
    managed.set(QStringLiteral("stack"), nq::yaml::Node::string(stack));
    managed.set(QStringLiteral("auto-route"), nq::yaml::Node::boolean(true));
    managed.set(QStringLiteral("auto-detect-interface"),
                nq::yaml::Node::boolean(true));
    managed.set(QStringLiteral("dns-hijack"), stringSequence(hijack));
    if (!excludes.isEmpty())
        managed.set(QStringLiteral("route-exclude-address"),
                    stringSequence(excludes));
    if (!routes.isEmpty())
        managed.set(QStringLiteral("route-address"), stringSequence(routes));

    const QStringList owned = {
        QStringLiteral("enable"),
        QStringLiteral("stack"),
        QStringLiteral("auto-route"),
        QStringLiteral("auto-detect-interface"),
        QStringLiteral("dns-hijack"),
        QStringLiteral("route-address"),
        QStringLiteral("route-exclude-address"),
        QStringLiteral("inet4-route-address"),
        QStringLiteral("inet6-route-address"),
        QStringLiteral("inet4-route-exclude-address"),
        QStringLiteral("inet6-route-exclude-address"),
    };
    for (const auto& entry : tun.mapping) {
        if (owned.contains(entry.first))
            continue;
        managed.set(entry.first, entry.second);
    }

    nq::yaml::Document document;
    document.root = nq::yaml::Node::object();
    document.root.set(QStringLiteral("tun"), managed);
    const QByteArray dumped = document.dump();
    if (dumped.isEmpty()) {
        *error = QStringLiteral("无法序列化 TUN 配置");
        return false;
    }

    *text = QString::fromUtf8(dumped);
    if (!text->endsWith(QLatin1Char('\n')))
        text->append(QLatin1Char('\n'));

    if (!excludes.isEmpty()) {
        warnings->append(
            QStringLiteral("已保留 %1 条 TUN 排除路由").arg(excludes.size()));
    }
    return true;
}

}  // namespace

namespace RuntimeConfig {

QString build(const QString& source, const cq::AppSettings& settings,
              QString* error, QStringList* warnings)
{
    if (error)
        error->clear();
    QStringList localWarnings;
    QStringList* notes = warnings ? warnings : &localWarnings;

    QString text = source;
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);

    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString& line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
    }
    const QString normalizedSource = lines.join(QLatin1Char('\n'));

    // Parse a copy of the profile only when TUN is on, so TUN options written
    // in block style, inline flow style, anchors or merge keys are all seen
    // the same way. Profiles without TUN skip the extra tree.
    nq::yaml::Document document;
    bool parsed = false;
    if (settings.tunEnabled)
        parsed = document.parse(normalizedSource.toUtf8());
    const bool usableDocument = parsed && document.root.isMapping();

    const nq::yaml::Node* sourceTun = nullptr;
    bool hadDns = false;
    if (usableDocument) {
        sourceTun = document.root.find(QStringLiteral("tun"));
        hadDns = document.root.find(QStringLiteral("dns")) != nullptr;
    } else {
        hadDns = hasKey(lines, QStringLiteral("dns"));
        if (settings.tunEnabled && !parsed) {
            notes->append(QStringLiteral("订阅 YAML 无法解析，已跳过 TUN 高级键保留：%1")
                              .arg(document.error.toString()));
        }
    }

    QString tunText;
    if (settings.tunEnabled) {
        QString tunError;
        if (!buildTunSection(sourceTun, &tunText, &tunError, notes)) {
            if (error)
                *error = tunError;
            return {};
        }
    }

    removeKeys(lines, {QStringLiteral("mixed-port"), QStringLiteral("port"),
                       QStringLiteral("socks-port"), QStringLiteral("redir-port"),
                       QStringLiteral("tproxy-port"), QStringLiteral("allow-lan"),
                       QStringLiteral("bind-address"), QStringLiteral("mode"),
                       QStringLiteral("log-level"), QStringLiteral("ipv6"),
                       QStringLiteral("external-controller"),
                       QStringLiteral("external-controller-tls"),
                       QStringLiteral("external-controller-unix"),
                       QStringLiteral("external-controller-pipe"),
                       QStringLiteral("external-controller-cors"),
                       QStringLiteral("secret"), QStringLiteral("tun")});

    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();

    QStringList generated;
    generated << QString();
    generated << QString::fromLatin1(kManagedHeader);
    generated << QStringLiteral("mixed-port: %1").arg(settings.mixedPort);
    generated << QStringLiteral("allow-lan: %1").arg(settings.allowLan ? QStringLiteral("true")
                                                                       : QStringLiteral("false"));
    generated << QStringLiteral("bind-address: %1").arg(settings.allowLan ? QStringLiteral("\"*\"")
                                                                          : QStringLiteral("127.0.0.1"));
    generated << QStringLiteral("mode: %1").arg(settings.mode);
    generated << QStringLiteral("log-level: %1").arg(settings.logLevel);
    generated << QStringLiteral("ipv6: %1").arg(settings.ipv6 ? QStringLiteral("true")
                                                              : QStringLiteral("false"));
    generated << QStringLiteral("external-controller: %1")
                     .arg(quoteYaml(QStringLiteral("127.0.0.1:%1").arg(settings.controllerPort)));
    generated << QStringLiteral("secret: %1").arg(quoteYaml(settings.secret));

    if (settings.tunEnabled) {
        if (!hadDns)
            generated.append(dnsBlock());
        QStringList tunLines = tunText.split(QLatin1Char('\n'));
        while (!tunLines.isEmpty() && tunLines.last().isEmpty())
            tunLines.removeLast();
        if (!tunLines.isEmpty())
            generated.append(tunLines);
    }

    lines += generated;
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

bool write(const QString& sourcePath, const QString& destinationPath,
           const cq::AppSettings& settings, QString* error,
           QStringList* warnings)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取当前配置：%1").arg(source.errorString());
        return false;
    }
    const QString content = QString::fromUtf8(source.readAll());
    if (content.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("当前配置为空");
        return false;
    }

    const QString runtime = build(content, settings, error, warnings);
    if (error && !error->isEmpty())
        return false;
    if (runtime.isEmpty()) {
        if (error)
            *error = QStringLiteral("运行时配置为空");
        return false;
    }

    QDir().mkpath(QFileInfo(destinationPath).absolutePath());
    QSaveFile destination(destinationPath);
    if (!destination.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法创建运行时配置");
        return false;
    }
    const QByteArray bytes = runtime.toUtf8();
    if (destination.write(bytes) != bytes.size() || !destination.commit()) {
        if (error)
            *error = QStringLiteral("写入运行时配置失败");
        return false;
    }
    return true;
}

}  // namespace RuntimeConfig
