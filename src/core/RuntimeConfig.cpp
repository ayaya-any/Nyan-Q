#include "core/RuntimeConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>
#include <QTextStream>

namespace {
QString quoteYaml(const QString& value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(escaped);
}

bool isTopLevelKey(const QString& line)
{
    if (line.isEmpty())
        return false;
    const QChar first = line.at(0);
    if (first == QLatin1Char(' ') || first == QLatin1Char('\t') || first == QLatin1Char('#'))
        return false;
    if (line.startsWith(QStringLiteral("---")) || line.startsWith(QStringLiteral("...")))
        return false;
    return line.contains(QLatin1Char(':'));
}

bool hasKey(const QStringList& lines, const QString& key)
{
    const QString prefix = key + QLatin1Char(':');
    for (const QString& line : lines) {
        if (line.startsWith(prefix))
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

        bool matches = false;
        for (const QString& key : keys) {
            if (line.startsWith(key + QLatin1Char(':'))) {
                matches = true;
                break;
            }
        }
        if (matches) {
            skipping = true;
            continue;
        }
        output.append(line);
    }

    // Remove trailing blank lines that belonged to the final skipped block.
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
        QStringLiteral("  listen: 0.0.0.0:1053"),
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
    };
}

QStringList tunBlock()
{
    return {
        QStringLiteral("tun:"),
        QStringLiteral("  enable: true"),
        QStringLiteral("  stack: mixed"),
        QStringLiteral("  auto-route: true"),
        QStringLiteral("  auto-detect-interface: true"),
        QStringLiteral("  dns-hijack:"),
        QStringLiteral("    - any:53"),
    };
}
}  // namespace

namespace RuntimeConfig {

QString build(const QString& source, const cq::AppSettings& settings)
{
    QString text = source;
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);

    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString& line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
    }

    const bool hadDns = hasKey(lines, QStringLiteral("dns"));

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
    generated << QStringLiteral("# ---- Nyan-Q managed runtime settings ----");
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
        generated.append(tunBlock());
    }

    lines += generated;
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

bool write(const QString& sourcePath, const QString& destinationPath,
           const cq::AppSettings& settings, QString* error)
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

    const QString runtime = build(content, settings);

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
