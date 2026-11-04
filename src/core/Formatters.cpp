#include "core/Formatters.h"

#include "app/Theme.h"

#include <QDate>
#include <QLocale>
#include <QtMath>

namespace {
constexpr qint64 kKB = 1024;
constexpr qint64 kMB = 1024 * 1024;
constexpr qint64 kGB = 1024LL * 1024 * 1024;
constexpr qint64 kTB = 1024LL * 1024 * 1024 * 1024;
}  // namespace

namespace Formatters {

QString bytes(qint64 value, int decimals)
{
    if (value < 0)
        return QStringLiteral("—");
    const double v = static_cast<double>(value);
    if (value < kKB)
        return QLocale().toString(value) + QStringLiteral(" B");
    if (value < kMB) {
        const int d = decimals >= 0 ? decimals : 1;
        return QString::number(v / kKB, 'f', d) + QStringLiteral(" KB");
    }
    if (value < kGB) {
        const int d = decimals >= 0 ? decimals : 1;
        return QString::number(v / kMB, 'f', d) + QStringLiteral(" MB");
    }
    if (value < kTB) {
        const int d = decimals >= 0 ? decimals : 2;
        return QString::number(v / kGB, 'f', d) + QStringLiteral(" GB");
    }
    return QString::number(v / kTB, 'f', 2) + QStringLiteral(" TB");
}

QString rate(qint64 bytesPerSecond)
{
    return bytes(qMax<qint64>(0, bytesPerSecond), 1) + QStringLiteral("/s");
}

QString duration(qint64 seconds)
{
    if (seconds <= 0)
        return QStringLiteral("0 分钟");
    const qint64 days = seconds / 86400;
    const qint64 hours = (seconds % 86400) / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    QStringList parts;
    if (days > 0)
        parts << QStringLiteral("%1 天").arg(days);
    if (hours > 0)
        parts << QStringLiteral("%1 小时").arg(hours);
    if (minutes > 0 && days == 0)
        parts << QStringLiteral("%1 分钟").arg(minutes);
    if (parts.isEmpty())
        parts << QStringLiteral("%1 秒").arg(seconds);
    return parts.join(QStringLiteral(" "));
}

QString clockDuration(qint64 seconds)
{
    seconds = qMax<qint64>(0, seconds);
    const qint64 h = seconds / 3600;
    const qint64 m = (seconds % 3600) / 60;
    const qint64 s = seconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

QString dateTime(const QDateTime& value)
{
    if (!value.isValid())
        return QStringLiteral("从未更新");
    return value.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

QString relativeTime(const QDateTime& value)
{
    if (!value.isValid())
        return QStringLiteral("从未");
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 secs = value.secsTo(now);
    if (secs < 0)
        return dateTime(value);
    if (secs < 60)
        return QStringLiteral("刚刚");
    if (secs < 3600)
        return QStringLiteral("%1 分钟前").arg(secs / 60);
    if (secs < 86400)
        return QStringLiteral("%1 小时前").arg(secs / 3600);
    if (secs < 86400 * 7)
        return QStringLiteral("%1 天前").arg(secs / 86400);
    return dateTime(value);
}

QString expiry(qint64 unixSeconds)
{
    if (unixSeconds <= 0)
        return QStringLiteral("长期有效");
    const QDateTime value = QDateTime::fromSecsSinceEpoch(unixSeconds);
    if (value < QDateTime::currentDateTime())
        return QStringLiteral("已过期");
    return value.toLocalTime().toString(QStringLiteral("yyyy-MM-dd"));
}

QString compactNumber(qint64 value)
{
    const double v = static_cast<double>(value);
    if (value >= 100000000)
        return QString::number(v / 100000000.0, 'f', 1) + QStringLiteral("亿");
    if (value >= 10000)
        return QString::number(v / 10000.0, 'f', 1) + QStringLiteral("万");
    return QLocale().toString(value);
}

QString endpoint(int port)
{
    return QStringLiteral("127.0.0.1:%1").arg(port);
}

int delayQuality(int delayMs)
{
    if (delayMs == 0)
        return 4;  // timeout
    if (delayMs < 0)
        return 0;  // untested
    if (delayMs < 200)
        return 1;
    if (delayMs < 500)
        return 2;
    return 3;
}

QColor delayColor(int delayMs)
{
    const Palette& p = pal();
    switch (delayQuality(delayMs)) {
    case 1:
        return p.dark ? QColor(QStringLiteral("#6FE0A6")) : QColor(QStringLiteral("#1F8A55"));
    case 2:
        return p.dark ? QColor(QStringLiteral("#F7CA74")) : QColor(QStringLiteral("#9A6A08"));
    case 3:
        return p.dark ? QColor(QStringLiteral("#FFB4A5")) : QColor(QStringLiteral("#B84A32"));
    case 4:
        return p.error;
    default:
        return p.onSurfaceVariant;
    }
}

QString percent(qint64 part, qint64 total)
{
    if (total <= 0)
        return QStringLiteral("不限");
    return QStringLiteral("%1%").arg(qBound(0.0, 100.0 * static_cast<double>(part) / static_cast<double>(total), 100.0), 0, 'f', 1);
}

}  // namespace Formatters

namespace cq {

QString coreStateLabel(CoreState state)
{
    switch (state) {
    case CoreState::Stopped:
        return QStringLiteral("已断开");
    case CoreState::Starting:
        return QStringLiteral("正在连接");
    case CoreState::Running:
        return QStringLiteral("已保护");
    case CoreState::Stopping:
        return QStringLiteral("正在断开");
    case CoreState::Failed:
        return QStringLiteral("连接异常");
    }
    return QStringLiteral("未知");
}

QString modeDisplayName(const QString& mode)
{
    if (mode.compare(QStringLiteral("global"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("全局");
    if (mode.compare(QStringLiteral("direct"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("直连");
    return QStringLiteral("规则");
}

QString proxyTypeDisplay(const QString& type)
{
    const QString lower = type.toLower();
    if (lower.contains(QStringLiteral("selector")))
        return QStringLiteral("手动选择");
    if (lower.contains(QStringLiteral("urltest")))
        return QStringLiteral("自动测速");
    if (lower.contains(QStringLiteral("fallback")))
        return QStringLiteral("故障转移");
    if (lower.contains(QStringLiteral("loadbalance")))
        return QStringLiteral("负载均衡");
    if (lower.contains(QStringLiteral("relay")))
        return QStringLiteral("链式代理");
    if (lower == QStringLiteral("direct"))
        return QStringLiteral("直连");
    if (lower == QStringLiteral("reject"))
        return QStringLiteral("拒绝");
    if (lower.contains(QStringLiteral("shadowsocks")))
        return QStringLiteral("Shadowsocks");
    if (lower.contains(QStringLiteral("trojan")))
        return QStringLiteral("Trojan");
    if (lower.contains(QStringLiteral("vmess")))
        return QStringLiteral("VMess");
    if (lower.contains(QStringLiteral("vless")))
        return QStringLiteral("VLESS");
    if (lower.contains(QStringLiteral("hysteria2")) || lower.contains(QStringLiteral("hy2")))
        return QStringLiteral("Hysteria2");
    if (lower.contains(QStringLiteral("hysteria")))
        return QStringLiteral("Hysteria");
    if (lower.contains(QStringLiteral("wireguard")))
        return QStringLiteral("WireGuard");
    if (lower.contains(QStringLiteral("tuic")))
        return QStringLiteral("TUIC");
    return type;
}

QString profileTypeLabel(const QString& type)
{
    if (type == QStringLiteral("remote"))
        return QStringLiteral("订阅");
    if (type == QStringLiteral("local"))
        return QStringLiteral("本地");
    return QStringLiteral("内置");
}

}  // namespace cq
