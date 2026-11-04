#include "ui/widgets/SiteLatencyTile.h"

#include "app/Fonts.h"
#include "app/Theme.h"
#include "core/Formatters.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QPainter>

namespace {
QColor alphaColor(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}
}  // namespace

SiteLatencyTile::SiteLatencyTile(const cq::SiteLatency& site, QWidget* parent)
    : QWidget(parent)
    , m_site(site)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumWidth(94);
    setFixedHeight(32);
    setToolTip(site.url);
}

void SiteLatencyTile::setSite(const cq::SiteLatency& site)
{
    m_site = site;
    setToolTip(site.testedAt.isValid()
                   ? QStringLiteral("%1\n%2\n测试时间：%3")
                         .arg(site.name, site.url,
                              site.testedAt.toString(QStringLiteral("HH:mm:ss")))
                   : QStringLiteral("%1\n%2").arg(site.name, site.url));
    update();
}

QSize SiteLatencyTile::sizeHint() const
{
    return QSize(124, 32);
}

QString SiteLatencyTile::badgeText() const
{
    if (m_site.id == QStringLiteral("github"))
        return QStringLiteral("GH");
    if (m_site.id == QStringLiteral("google"))
        return QStringLiteral("GO");
    if (m_site.id == QStringLiteral("youtube"))
        return QStringLiteral("YT");
    if (m_site.id == QStringLiteral("openai"))
        return QStringLiteral("AI");
    if (m_site.id == QStringLiteral("telegram"))
        return QStringLiteral("TG");
    if (m_site.id == QStringLiteral("cloudflare"))
        return QStringLiteral("CF");
    return m_site.name.left(2).toUpper();
}

void SiteLatencyTile::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const Palette& p = pal();

    const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(alphaColor(p.outlineVariant, 110), 1.0));
    painter.setBrush(p.surfaceContainerHigh);
    painter.drawRoundedRect(bounds, 9, 9);

    const QRectF badgeRect(6, 6, 20, 20);
    painter.setPen(Qt::NoPen);
    painter.setBrush(alphaColor(p.primary, 30));
    painter.drawRoundedRect(badgeRect, 6, 6);

    QFont badgeFont = Fonts::text(7.5, QFont::DemiBold);
    badgeFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.2);
    painter.setFont(badgeFont);
    painter.setPen(p.primary);
    painter.drawText(badgeRect, Qt::AlignCenter, badgeText());

    QString text;
    QColor color = p.onSurfaceVariant;
    if (m_site.pending) {
        text = QStringLiteral("测试中");
        color = p.warning;
    } else if (m_site.ok && m_site.delayMs >= 0) {
        text = QStringLiteral("%1 ms").arg(m_site.delayMs);
        color = Formatters::delayColor(m_site.delayMs);
    } else if (m_site.testedAt.isValid()) {
        text = QStringLiteral("超时");
        color = p.error;
    } else {
        text = QStringLiteral("未连接");
    }

    const QFont delayFont = Fonts::text(10, QFont::DemiBold);
    painter.setFont(delayFont);
    painter.setPen(color);
    const QFontMetrics delayMetrics(delayFont);
    const int delayWidth = delayMetrics.horizontalAdvance(text);
    const QRectF delayRect(width() - delayWidth - 8, 0, delayWidth + 2, height());
    painter.drawText(delayRect, Qt::AlignRight | Qt::AlignVCenter, text);

    const qreal nameLeft = badgeRect.right() + 7;
    const qreal nameRight = width() - delayWidth - 18;
    const QFont nameFont = Fonts::text(10.5, QFont::DemiBold);
    painter.setFont(nameFont);
    painter.setPen(p.onSurface);
    const QFontMetrics nameMetrics(nameFont);
    const QString name = nameMetrics.elidedText(m_site.name, Qt::ElideRight,
                                                qMax(10, int(nameRight - nameLeft)));
    painter.drawText(QRectF(nameLeft, 0, qMax(10.0, nameRight - nameLeft), height()),
                     Qt::AlignLeft | Qt::AlignVCenter, name);
}
