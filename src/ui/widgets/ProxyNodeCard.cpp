#include "ui/widgets/ProxyNodeCard.h"

#include "app/Fonts.h"
#include "app/Theme.h"
#include "core/Formatters.h"

#include <QEnterEvent>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

ProxyNodeCard::ProxyNodeCard(const cq::ProxyNode& node, bool selected, QWidget* parent)
    : QWidget(parent)
    , m_node(node)
    , m_selected(selected)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    refreshToolTip();
}

void ProxyNodeCard::setNode(const cq::ProxyNode& node, bool selected)
{
    m_node = node;
    m_selected = selected;
    refreshToolTip();
    update();
}

void ProxyNodeCard::setDelay(int delay)
{
    m_node.delay = delay;
    refreshToolTip();
    update();
}

void ProxyNodeCard::refreshToolTip()
{
    QString delay = QStringLiteral("未测速");
    if (m_node.delay == 0)
        delay = QStringLiteral("超时");
    else if (m_node.delay > 0)
        delay = QStringLiteral("%1 ms").arg(m_node.delay);
    setToolTip(QStringLiteral("%1\n%2 · %3\n左键切换节点 · 右键测速")
                   .arg(m_node.name, cq::proxyTypeDisplay(m_node.type), delay));
}

QSize ProxyNodeCard::sizeHint() const
{
    return QSize(208, 84);
}

QSize ProxyNodeCard::minimumSizeHint() const
{
    return sizeHint();
}

void ProxyNodeCard::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const Palette& p = pal();

    QColor background = p.surfaceContainer;
    QColor border = p.outlineVariant;
    QColor titleColor = p.onSurface;
    QColor secondary = p.onSurfaceVariant;
    if (m_hovered) {
        background = p.surfaceContainerHigh;
        border = Theme::blend(p.outlineVariant, p.outline, 0.6);
    }
    if (m_selected) {
        background = p.secondaryContainer;
        border = Theme::alpha(p.secondary, 170);
        titleColor = p.onSecondaryContainer;
        secondary = Theme::alpha(p.onSecondaryContainer, 195);
    }

    const QRectF bounds = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(border, m_selected ? 1.5 : 1.0));
    painter.setBrush(background);
    painter.drawRoundedRect(bounds, 16, 16);

    QFont nameFont = Fonts::text(13, QFont::DemiBold);
    const QFontMetricsF nameMetrics(nameFont);
    qreal nameWidth = width() - 32;
    if (m_selected)
        nameWidth -= 20;
    painter.setFont(nameFont);
    painter.setPen(titleColor);
    const QString elidedName =
        nameMetrics.elidedText(m_node.name, Qt::ElideRight, static_cast<int>(nameWidth));
    painter.drawText(QRectF(16, 13, nameWidth, 20), Qt::AlignVCenter | Qt::AlignLeft,
                     elidedName);

    if (m_selected) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(p.secondary);
        painter.drawEllipse(QPointF(width() - 20, 23), 8, 8);
        painter.setFont(Fonts::icon(11));
        painter.setPen(p.onSecondary);
        painter.drawText(QRectF(width() - 28, 15, 16, 16), Qt::AlignCenter,
                         QStringLiteral("check"));
    }

    painter.setFont(Fonts::text(11));
    painter.setPen(secondary);
    const QFontMetricsF typeMetrics(Fonts::text(11));
    const QString typeText = cq::proxyTypeDisplay(m_node.type);
    const qreal typeWidth = width() - 116;
    painter.drawText(QRectF(16, height() - 30, typeWidth, 16),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     typeMetrics.elidedText(typeText, Qt::ElideRight, static_cast<int>(typeWidth)));

    const bool direct =
        m_node.type.compare(QStringLiteral("Direct"), Qt::CaseInsensitive) == 0 ||
        m_node.name.compare(QStringLiteral("DIRECT"), Qt::CaseInsensitive) == 0;
    const bool reject =
        m_node.type.compare(QStringLiteral("Reject"), Qt::CaseInsensitive) == 0 ||
        m_node.name.compare(QStringLiteral("REJECT"), Qt::CaseInsensitive) == 0;
    const QString delayText = direct ? QStringLiteral("直连")
                              : reject ? QStringLiteral("拒绝")
                              : m_node.delay < 0 ? QStringLiteral("未测速")
                              : m_node.delay == 0 ? QStringLiteral("超时")
                                                  : QStringLiteral("%1 ms").arg(m_node.delay);
    const QColor delayColor =
        direct ? p.secondary : reject ? p.error : Formatters::delayColor(m_node.delay);
    const QFont delayFont = Fonts::text(11, QFont::DemiBold);
    const QFontMetricsF delayMetrics(delayFont);
    const qreal delayWidth = delayMetrics.horizontalAdvance(delayText) + 20;
    const QRectF delayRect(width() - delayWidth - 14, height() - 33, delayWidth, 20);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_selected ? Theme::alpha(p.onSecondaryContainer, 22)
                                : Theme::alpha(p.onSurface, 16));
    painter.drawRoundedRect(delayRect, 10, 10);
    painter.setBrush(delayColor);
    painter.drawEllipse(QPointF(delayRect.left() + 9, delayRect.center().y()), 2.8, 2.8);
    painter.setFont(delayFont);
    painter.setPen(m_selected ? p.onSecondaryContainer : p.onSurfaceVariant);
    painter.drawText(delayRect.adjusted(16, 0, -7, 0), Qt::AlignVCenter | Qt::AlignLeft,
                     delayText);
}

void ProxyNodeCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit selected(m_node.name);
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton) {
        emit testRequested(m_node.name);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ProxyNodeCard::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit testRequested(m_node.name);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ProxyNodeCard::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void ProxyNodeCard::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    QWidget::leaveEvent(event);
}
