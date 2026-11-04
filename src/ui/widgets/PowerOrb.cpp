#include "ui/widgets/PowerOrb.h"

#include "app/Fonts.h"
#include "app/Theme.h"

#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRadialGradient>
#include <QVariantAnimation>

#include <cmath>

PowerOrb::PowerOrb(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(160, 160);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);

    m_animation = new QVariantAnimation(this);
    m_animation->setStartValue(0.0);
    m_animation->setEndValue(1.0);
    m_animation->setDuration(2600);
    m_animation->setLoopCount(-1);
    m_animation->setEasingCurve(QEasingCurve::InOutSine);
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_phase = value.toReal();
        update();
    });
    m_animation->start();
}

void PowerOrb::setState(cq::CoreState state)
{
    if (m_state == state)
        return;
    m_state = state;
    if (state == cq::CoreState::Starting) {
        m_animation->setDuration(1300);
    } else {
        m_animation->setDuration(2600);
    }
    update();
}

void PowerOrb::setInteractive(bool interactive)
{
    m_interactive = interactive;
    setCursor(interactive ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}

QSize PowerOrb::sizeHint() const
{
    return QSize(160, 160);
}

void PowerOrb::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const QPointF center = rect().center();
    const qreal baseRadius = 57.0;
    const bool active = m_state == cq::CoreState::Running;
    const bool busy = m_state == cq::CoreState::Starting || m_state == cq::CoreState::Stopping;
    const qreal pulse = (std::sin(m_phase * 6.283185307) + 1.0) / 2.0;

    const QColor ringColor = active ? QColor(255, 255, 255, 120) : QColor(255, 255, 255, 58);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Theme::alpha(ringColor, active ? 45 : 30), 1.0));
    painter.drawEllipse(center, baseRadius + 22, baseRadius + 22);
    painter.setPen(QPen(Theme::alpha(ringColor, active ? 70 : 38), 1.2));
    painter.drawEllipse(center, baseRadius + 12, baseRadius + 12);

    if (busy) {
        painter.setPen(QPen(QColor(255, 255, 255, 210), 3.0, Qt::SolidLine, Qt::RoundCap));
        const QRectF arcRect(center.x() - baseRadius - 12, center.y() - baseRadius - 12,
                             (baseRadius + 12) * 2, (baseRadius + 12) * 2);
        painter.drawArc(arcRect, static_cast<int>(-m_phase * 360 * 16),
                        static_cast<int>(78 * 16));
    } else if (active) {
        QRadialGradient glow(center, baseRadius + 12);
        glow.setColorAt(0.0, QColor(255, 255, 255, 24));
        glow.setColorAt(0.72, QColor(255, 255, 255, 14 + static_cast<int>(pulse * 20)));
        glow.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(glow);
        painter.drawEllipse(center, baseRadius + 20, baseRadius + 20);
    }

    const qreal radius = baseRadius;
    QColor fill = active ? QColor(255, 255, 255) : QColor(255, 255, 255, 22);
    if (m_hovered)
        fill = active ? QColor(255, 255, 255) : QColor(255, 255, 255, 34);
    painter.setPen(QPen(QColor(255, 255, 255, active ? 0 : 105), 1.2));
    painter.setBrush(fill);
    painter.drawEllipse(center, radius, radius);

    if (active) {
        QRadialGradient sheen(center - QPointF(16, 22), radius * 1.7);
        sheen.setColorAt(0.0, QColor(255, 255, 255, 230));
        sheen.setColorAt(1.0, QColor(255, 255, 255));
        painter.setPen(Qt::NoPen);
        painter.setBrush(sheen);
        painter.drawEllipse(center, radius - 1, radius - 1);
    }

    QColor glyphColor = active ? pal().heroStart : QColor(255, 255, 255, 235);
    if (m_state == cq::CoreState::Failed)
        glyphColor = QColor(255, 210, 205);
    painter.setFont(Fonts::icon(38));
    painter.setPen(glyphColor);
    painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("power_settings_new"));

    if (active) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(102, 227, 193));
        painter.drawEllipse(center + QPointF(radius - 13, -radius + 13), 7, 7);
        painter.setPen(QPen(Qt::white, 1.8));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center + QPointF(radius - 13, -radius + 13), 7, 7);
    }
}

void PowerOrb::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_interactive) {
        emit clicked();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PowerOrb::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void PowerOrb::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    QWidget::leaveEvent(event);
}
