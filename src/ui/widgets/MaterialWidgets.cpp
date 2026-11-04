#include "ui/widgets/MaterialWidgets.h"

#include "app/Fonts.h"
#include "app/Theme.h"

#include <QApplication>
#include <QEnterEvent>
#include <QFontMetricsF>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QStyleHints>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

namespace {
QFont uiFont(int px, QFont::Weight weight = QFont::Normal)
{
    return Fonts::text(px, weight);
}

int radiusFor(int height)
{
    return std::min(12, height / 2);
}

QColor mixed(const QColor& a, const QColor& b, qreal t)
{
    return Theme::blend(a, b, qBound(0.0, t, 1.0));
}

QColor alpha(const QColor& color, int a)
{
    QColor copy = color;
    copy.setAlpha(qBound(0, a, 255));
    return copy;
}

QColor readableOn(const QColor& background)
{
    const int luminance = (background.red() * 299 + background.green() * 587 +
                           background.blue() * 114) /
                          1000;
    return luminance > 150 ? QColor(30, 25, 40) : Qt::white;
}

QString elided(const QFontMetricsF& metrics, const QString& text, qreal width)
{
    if (metrics.horizontalAdvance(text) <= width)
        return text;
    return metrics.elidedText(text, Qt::ElideRight, static_cast<int>(width));
}
}  // namespace

// ---------------------------------------------------------------------------
// M3Button
// ---------------------------------------------------------------------------

M3Button::M3Button(QWidget* parent)
    : M3Button(QString(), Variant::Filled, parent)
{
}

M3Button::M3Button(const QString& text, Variant variant, QWidget* parent)
    : QAbstractButton(parent)
    , m_variant(variant)
{
    setText(text);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    if (!text.isEmpty())
        setAccessibleName(text);

    m_hoverAnimation = new QVariantAnimation(this);
    m_hoverAnimation->setDuration(120);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_hoverAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        setHoverProgress(value.toReal());
    });
}

void M3Button::setVariant(Variant variant)
{
    if (m_variant == variant)
        return;
    m_variant = variant;
    update();
}

void M3Button::setIconText(const QString& glyph)
{
    m_icon = glyph;
    updateGeometry();
    update();
}

void M3Button::setCompact(bool compact)
{
    if (m_compact == compact)
        return;
    m_compact = compact;
    updateGeometry();
    update();
}

void M3Button::setAccent(const QColor& background, const QColor& foreground)
{
    m_hasAccent = true;
    m_accentBackground = background;
    m_accentForeground = foreground;
    update();
}

void M3Button::clearAccent()
{
    m_hasAccent = false;
    update();
}

void M3Button::setPill(bool pill)
{
    m_pill = pill;
    update();
}

void M3Button::setLeadingIconOnly(bool leadingOnly)
{
    m_leadingIconOnly = leadingOnly;
    updateGeometry();
    update();
}

void M3Button::setHoverProgress(qreal value)
{
    m_hoverProgress = value;
    update();
}

QFont M3Button::buttonFont() const
{
    return uiFont(m_compact ? 12 : 13, QFont::DemiBold);
}

QSize M3Button::sizeHint() const
{
    const QFontMetricsF metrics(buttonFont());
    qreal width = metrics.horizontalAdvance(text());
    if (!m_icon.isEmpty()) {
        if (m_leadingIconOnly) {
            width = m_compact ? 18 : 20;
        } else {
            width += 8.0 + (m_compact ? 16 : 18);
        }
    }
    width += (m_compact ? 14 : 20) * 2;
    if (width < 56)
        width = 56;
    return QSize(static_cast<int>(std::ceil(width)), m_compact ? 32 : 40);
}

QSize M3Button::minimumSizeHint() const
{
    return QSize(m_compact ? 48 : 56, m_compact ? 32 : 40);
}

QColor M3Button::backgroundForState() const
{
    const Palette& p = pal();
    if (m_hasAccent)
        return m_accentBackground;
    switch (m_variant) {
    case Variant::Filled:
        return p.primary;
    case Variant::Tonal:
        return p.secondaryContainer;
    case Variant::Outlined:
    case Variant::Text:
        return Qt::transparent;
    case Variant::Elevated:
        return p.surfaceContainerHigh;
    case Variant::Danger:
        return p.error;
    }
    return p.primary;
}

QColor M3Button::foregroundForState() const
{
    const Palette& p = pal();
    if (m_hasAccent)
        return m_accentForeground;
    switch (m_variant) {
    case Variant::Filled:
        return p.onPrimary;
    case Variant::Tonal:
        return p.onSecondaryContainer;
    case Variant::Outlined:
        return p.onSurface;
    case Variant::Text:
        return p.primary;
    case Variant::Elevated:
        return p.primary;
    case Variant::Danger:
        return p.onError;
    }
    return p.onPrimary;
}

QColor M3Button::borderForState() const
{
    const Palette& p = pal();
    if (m_variant == Variant::Outlined)
        return mixed(p.outline, p.primary, m_hoverProgress * 0.7);
    return Qt::transparent;
}

void M3Button::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const QRectF bounds = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    const int radius = m_pill ? static_cast<int>(height() / 2.0) : radiusFor(height());
    QColor background = backgroundForState();
    QColor foreground = foregroundForState();
    QColor border = borderForState();

    if (!isEnabled()) {
        background = alpha(pal().onSurface, 18);
        foreground = alpha(pal().onSurface, 105);
        border = pal().outlineVariant;
    } else if (m_variant == Variant::Outlined || m_variant == Variant::Text) {
        if (m_hoverProgress > 0.001)
            background = alpha(m_hasAccent ? m_accentForeground : pal().primary,
                               static_cast<int>(20 * m_hoverProgress));
    } else {
        const QColor overlay = m_variant == Variant::Danger ? pal().onError
                                                            : foreground;
        background = mixed(background, overlay, m_hoverProgress * 0.10);
    }

    painter.setPen(border.alpha() > 0 ? QPen(border, 1.0) : Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(bounds, radius, radius);

    if (hasFocus()) {
        painter.setPen(QPen(alpha(pal().primary, 160), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(bounds.adjusted(3, 3, -3, -3), radius - 2, radius - 2);
    }

    const QFont font = buttonFont();
    const QFontMetricsF metrics(font);
    const QString label = m_leadingIconOnly ? QString() : text();
    const qreal iconWidth = m_icon.isEmpty() ? 0.0 : (m_compact ? 16.0 : 18.0);
    const qreal gap = (!m_icon.isEmpty() && !label.isEmpty()) ? 8.0 : 0.0;
    const qreal total = iconWidth + gap + (label.isEmpty() ? 0.0 : metrics.horizontalAdvance(label));
    qreal x = (width() - total) / 2.0;

    if (!m_icon.isEmpty()) {
        painter.setFont(Fonts::icon(static_cast<int>(iconWidth)));
        painter.setPen(foreground);
        painter.drawText(QRectF(x, 0, iconWidth, height()), Qt::AlignCenter, m_icon);
        x += iconWidth + gap;
    }
    if (!label.isEmpty()) {
        painter.setFont(font);
        painter.setPen(foreground);
        const qreal available = width() - x - 8;
        painter.drawText(QRectF(x, 0, available, height()), Qt::AlignVCenter | Qt::AlignLeft,
                         elided(metrics, label, available));
    }
}

void M3Button::enterEvent(QEnterEvent* event)
{
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(1.0);
    m_hoverAnimation->start();
    QAbstractButton::enterEvent(event);
}

void M3Button::leaveEvent(QEvent* event)
{
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(0.0);
    m_hoverAnimation->start();
    QAbstractButton::leaveEvent(event);
}

void M3Button::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::EnabledChange || event->type() == QEvent::FontChange)
        update();
    QAbstractButton::changeEvent(event);
}

// ---------------------------------------------------------------------------
// M3IconButton
// ---------------------------------------------------------------------------

M3IconButton::M3IconButton(const QString& glyph, QWidget* parent)
    : QAbstractButton(parent)
    , m_glyph(glyph)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_hoverAnimation = new QVariantAnimation(this);
    m_hoverAnimation->setDuration(120);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_hoverAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        setHoverProgress(value.toReal());
    });
}

void M3IconButton::setGlyph(const QString& glyph)
{
    m_glyph = glyph;
    update();
}

void M3IconButton::setGlyphSize(int pixelSize)
{
    m_glyphSize = qBound(12, pixelSize, 48);
    update();
}

void M3IconButton::setDiameter(int diameter)
{
    m_diameter = qBound(24, diameter, 72);
    setFixedSize(m_diameter, m_diameter);
    updateGeometry();
    update();
}

void M3IconButton::setKind(Kind kind)
{
    m_kind = kind;
    update();
}

void M3IconButton::setDangerOnHover(bool danger)
{
    m_dangerOnHover = danger;
    update();
}

void M3IconButton::setIconColor(const QColor& color)
{
    m_hasIconColor = true;
    m_iconColor = color;
    update();
}

void M3IconButton::setHoverProgress(qreal value)
{
    m_hoverProgress = value;
    update();
}

QSize M3IconButton::sizeHint() const
{
    return QSize(m_diameter, m_diameter);
}

QSize M3IconButton::minimumSizeHint() const
{
    return sizeHint();
}

void M3IconButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const Palette& p = pal();
    QRectF circle = rect().adjusted(2, 2, -2, -2);
    QColor background = Qt::transparent;
    QColor foreground = m_hasIconColor ? m_iconColor : p.onSurfaceVariant;

    switch (m_kind) {
    case Kind::Tonal:
        background = p.secondaryContainer;
        foreground = m_hasIconColor ? m_iconColor : p.onSecondaryContainer;
        break;
    case Kind::Filled:
        background = p.primary;
        foreground = m_hasIconColor ? m_iconColor : p.onPrimary;
        break;
    case Kind::Text:
        foreground = m_hasIconColor ? m_iconColor : p.primary;
        break;
    case Kind::Standard:
    default:
        foreground = m_hasIconColor ? m_iconColor : p.onSurfaceVariant;
        break;
    }

    if (m_dangerOnHover && m_hoverProgress > 0.01) {
        background = alpha(p.error, static_cast<int>(30 * m_hoverProgress));
        foreground = p.error;
    } else if (m_kind == Kind::Standard || m_kind == Kind::Text) {
        if (m_hoverProgress > 0.01)
            background = alpha(m_kind == Kind::Text ? p.primary : p.onSurface,
                               static_cast<int>(m_kind == Kind::Text ? 34 : 22) *
                                   m_hoverProgress);
    } else {
        background = mixed(background, foreground, m_hoverProgress * 0.08);
    }

    if (!isEnabled()) {
        background = alpha(p.onSurface, 14);
        foreground = alpha(p.onSurface, 90);
    }

    if (background.alpha() > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawEllipse(circle);
    }

    if (hasFocus()) {
        painter.setPen(QPen(alpha(p.primary, 160), 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(circle.adjusted(1, 1, -1, -1));
    }

    painter.setFont(Fonts::icon(m_glyphSize));
    painter.setPen(foreground);
    painter.drawText(rect(), Qt::AlignCenter, m_glyph);
}

void M3IconButton::enterEvent(QEnterEvent* event)
{
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(1.0);
    m_hoverAnimation->start();
    QAbstractButton::enterEvent(event);
}

void M3IconButton::leaveEvent(QEvent* event)
{
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(0.0);
    m_hoverAnimation->start();
    QAbstractButton::leaveEvent(event);
}

// ---------------------------------------------------------------------------
// M3Switch
// ---------------------------------------------------------------------------

M3Switch::M3Switch(QWidget* parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(sizeHint());

    m_animation = new QPropertyAnimation(this, "progress", this);
    m_animation->setDuration(170);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(this, &QAbstractButton::toggled, this, [this](bool checked) {
        m_animation->stop();
        m_animation->setStartValue(m_progress);
        m_animation->setEndValue(checked ? 1.0 : 0.0);
        m_animation->start();
    });
}

QSize M3Switch::sizeHint() const
{
    return QSize(46, 26);
}

QSize M3Switch::minimumSizeHint() const
{
    return sizeHint();
}

void M3Switch::setProgress(qreal value)
{
    m_progress = qBound(0.0, value, 1.0);
    update();
}

void M3Switch::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const Palette& p = pal();

    const qreal trackHeight = 24.0;
    QRectF track(1, (height() - trackHeight) / 2.0, width() - 2, trackHeight);
    QColor offTrack = p.surfaceContainerHighest;
    QColor onTrack = p.primary;
    if (!isEnabled()) {
        offTrack = alpha(p.onSurface, 20);
        onTrack = alpha(p.primary, 80);
    }
    const QColor trackColor = mixed(offTrack, onTrack, m_progress);
    painter.setPen(QPen(mixed(p.outline, p.primary, m_progress), 1.2));
    painter.setBrush(trackColor);
    painter.drawRoundedRect(track, trackHeight / 2.0, trackHeight / 2.0);

    const qreal thumbSize = 18.0;
    const qreal margin = 4.0;
    const qreal travel = track.width() - thumbSize - margin * 2;
    const qreal thumbX = track.left() + margin + travel * m_progress;
    const QRectF thumb(thumbX, track.center().y() - thumbSize / 2.0, thumbSize, thumbSize);

    QColor offThumb = p.outline;
    QColor onThumb = p.onPrimary;
    const QColor thumbColor = mixed(offThumb, onThumb, m_progress);
    painter.setPen(Qt::NoPen);
    painter.setBrush(thumbColor);
    painter.drawEllipse(thumb);

    if (hasFocus()) {
        painter.setPen(QPen(alpha(p.primary, 150), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect().adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
    }
}

// ---------------------------------------------------------------------------
// M3Card
// ---------------------------------------------------------------------------

M3Card::M3Card(QWidget* parent, int radius)
    : QWidget(parent)
    , m_radius(radius)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

void M3Card::setRadius(int radius)
{
    m_radius = qMax(0, radius);
    update();
}

void M3Card::setSurfaceColor(const QColor& color)
{
    m_hasSurface = true;
    m_surfaceColor = color;
    update();
}

void M3Card::clearSurfaceColor()
{
    m_hasSurface = false;
    update();
}

void M3Card::setBorderColor(const QColor& color)
{
    m_borderColor = color;
    update();
}

void M3Card::clearBorderColor()
{
    m_borderColor = QColor();
    update();
}

void M3Card::setDrawBorder(bool draw)
{
    m_drawBorder = draw;
    update();
}

void M3Card::setPadding(int padding)
{
    m_padding = padding;
    if (layout())
        layout()->setContentsMargins(padding, padding, padding, padding);
    update();
}

void M3Card::setLayoutMargins(int horizontal, int vertical)
{
    if (layout())
        layout()->setContentsMargins(horizontal, vertical, horizontal, vertical);
}

void M3Card::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setBrush(m_hasSurface ? m_surfaceColor : pal().surfaceContainer);
    QColor border = m_borderColor.isValid() ? m_borderColor : pal().outlineVariant;
    if (!m_drawBorder)
        border = Qt::transparent;
    painter.setPen(border.alpha() > 0 ? QPen(border, 1.0) : Qt::NoPen);
    painter.drawRoundedRect(bounds, m_radius, m_radius);
}

// ---------------------------------------------------------------------------
// M3IconLabel
// ---------------------------------------------------------------------------

M3IconLabel::M3IconLabel(const QString& glyph, QWidget* parent)
    : QWidget(parent)
    , m_glyph(glyph)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(m_box);
}

void M3IconLabel::setGlyph(const QString& glyph)
{
    m_glyph = glyph;
    update();
}

void M3IconLabel::setGlyphSize(int pixelSize)
{
    m_glyphSize = pixelSize;
    update();
}

void M3IconLabel::setColor(const QColor& color)
{
    m_hasColor = true;
    m_color = color;
    update();
}

void M3IconLabel::clearColor()
{
    m_hasColor = false;
    update();
}

void M3IconLabel::setBadge(bool badge, const QColor& background)
{
    m_badge = badge;
    m_badgeColor = background;
    update();
}

void M3IconLabel::setFixedBox(int width, int height)
{
    m_box = QSize(width, height);
    setFixedSize(m_box);
    updateGeometry();
}

QSize M3IconLabel::sizeHint() const
{
    return m_box;
}

void M3IconLabel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const Palette& p = pal();
    if (m_badge) {
        QColor background = m_badgeColor.isValid() ? m_badgeColor : Theme::alpha(p.primary, 26);
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawRoundedRect(rect(), 10, 10);
    }
    painter.setFont(Fonts::icon(m_glyphSize));
    painter.setPen(m_hasColor ? m_color : (m_badge ? p.primary : p.onSurfaceVariant));
    painter.drawText(rect(), Qt::AlignCenter, m_glyph);
}

// ---------------------------------------------------------------------------
// M3SegmentedControl
// ---------------------------------------------------------------------------

M3SegmentedControl::M3SegmentedControl(const QStringList& options, QWidget* parent)
    : QWidget(parent)
    , m_options(options)
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_animation = new QPropertyAnimation(this, "indicator", this);
    m_animation->setDuration(190);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    setMinimumHeight(38);
}

void M3SegmentedControl::setOptions(const QStringList& options)
{
    m_options = options;
    m_currentIndex = qBound(0, m_currentIndex, qMax(0, m_options.size() - 1));
    setIndicator(m_currentIndex);
    updateGeometry();
    update();
}

void M3SegmentedControl::setIndicator(qreal value)
{
    m_indicator = value;
    update();
}

void M3SegmentedControl::setCurrentIndex(int index)
{
    if (m_options.isEmpty())
        return;
    index = qBound(0, index, m_options.size() - 1);
    if (m_currentIndex == index && qFuzzyCompare(m_indicator + 1.0, index + 1.0))
        return;
    m_currentIndex = index;
    m_animation->stop();
    m_animation->setStartValue(m_indicator);
    m_animation->setEndValue(static_cast<qreal>(index));
    m_animation->start();
}

void M3SegmentedControl::choose(int index, bool emitSignal)
{
    if (index < 0 || index >= m_options.size() || index == m_currentIndex)
        return;
    setCurrentIndex(index);
    if (emitSignal)
        emit currentChanged(index, m_options.at(index));
}

void M3SegmentedControl::setCompact(bool compact)
{
    m_compact = compact;
    setMinimumHeight(compact ? 32 : 38);
    updateGeometry();
    update();
}

QSize M3SegmentedControl::sizeHint() const
{
    const QFontMetricsF metrics(uiFont(12, QFont::DemiBold));
    qreal widest = 60;
    for (const QString& option : m_options)
        widest = std::max(widest, metrics.horizontalAdvance(option));
    return QSize(static_cast<int>(widest + 30) * std::max(1, static_cast<int>(m_options.size())),
                 m_compact ? 32 : 38);
}

void M3SegmentedControl::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const Palette& p = pal();
    const qreal height = m_compact ? 32 : 38;
    QRectF bounds(0.5, (this->height() - height) / 2.0 + 0.5, width() - 1, height - 1);
    painter.setPen(QPen(p.outlineVariant, 1));
    painter.setBrush(p.surfaceContainerHigh);
    painter.drawRoundedRect(bounds, 12, 12);

    if (m_options.isEmpty())
        return;

    const qreal inset = 3.0;
    const qreal segmentWidth = (bounds.width() - inset * 2) / m_options.size();
    const QRectF selected(bounds.left() + inset + segmentWidth * m_indicator,
                          bounds.top() + inset, segmentWidth, bounds.height() - inset * 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(p.primaryContainer);
    painter.drawRoundedRect(selected, 9, 9);

    QFont font = uiFont(12, QFont::Medium);
    for (int i = 0; i < m_options.size(); ++i) {
        const QRectF segment(bounds.left() + inset + segmentWidth * i, bounds.top() + inset,
                             segmentWidth, bounds.height() - inset * 2);
        if (i == m_currentIndex) {
            font.setWeight(QFont::DemiBold);
            painter.setPen(p.onPrimaryContainer);
        } else {
            font.setWeight(QFont::Normal);
            painter.setPen(i == m_hoverIndex ? p.onSurface : p.onSurfaceVariant);
        }
        painter.setFont(font);
        const QFontMetricsF metrics(font);
        painter.drawText(segment, Qt::AlignCenter,
                         elided(metrics, m_options.value(i), segment.width() - 12));
    }

    if (hasFocus()) {
        painter.setPen(QPen(alpha(p.primary, 170), 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(bounds.adjusted(1, 1, -1, -1), 11, 11);
    }
}

void M3SegmentedControl::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_options.isEmpty()) {
        QWidget::mousePressEvent(event);
        return;
    }
    const qreal inset = 3.0;
    const qreal segmentWidth = (width() - inset * 2) / m_options.size();
    const int index = qBound(0, static_cast<int>((event->position().x() - inset) / segmentWidth),
                             m_options.size() - 1);
    choose(index, true);
    event->accept();
}

void M3SegmentedControl::mouseMoveEvent(QMouseEvent* event)
{
    if (m_options.isEmpty())
        return;
    const qreal inset = 3.0;
    const qreal segmentWidth = (width() - inset * 2) / m_options.size();
    const int index = qBound(0, static_cast<int>((event->position().x() - inset) / segmentWidth),
                             m_options.size() - 1);
    if (m_hoverIndex != index) {
        m_hoverIndex = index;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void M3SegmentedControl::leaveEvent(QEvent* event)
{
    m_hoverIndex = -1;
    update();
    QWidget::leaveEvent(event);
}

void M3SegmentedControl::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Left) {
        choose(m_currentIndex - 1, true);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Right) {
        choose(m_currentIndex + 1, true);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// M3Pill
// ---------------------------------------------------------------------------

M3Pill::M3Pill(const QString& text, QWidget* parent)
    : QWidget(parent)
    , m_text(text)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedHeight(24);
}

void M3Pill::setText(const QString& text)
{
    m_text = text;
    updateGeometry();
    update();
}

void M3Pill::setTone(Tone tone)
{
    m_tone = tone;
    update();
}

void M3Pill::setDot(bool dot)
{
    m_dot = dot;
    updateGeometry();
    update();
}

void M3Pill::setDotColor(const QColor& color)
{
    m_hasDotColor = true;
    m_dotColor = color;
    update();
}

QSize M3Pill::sizeHint() const
{
    const QFontMetricsF metrics(uiFont(11, QFont::DemiBold));
    qreal width = metrics.horizontalAdvance(m_text) + 20;
    if (m_dot)
        width += 11;
    return QSize(static_cast<int>(std::ceil(width)), 24);
}

void M3Pill::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const Palette& p = pal();

    QColor background;
    QColor foreground;
    switch (m_tone) {
    case Tone::Primary:
        background = p.primaryContainer;
        foreground = p.onPrimaryContainer;
        break;
    case Tone::Success:
        background = alpha(p.success, 30);
        foreground = p.success;
        break;
    case Tone::Warning:
        background = alpha(p.warning, 30);
        foreground = p.warning;
        break;
    case Tone::Error:
        background = alpha(p.error, 30);
        foreground = p.error;
        break;
    case Tone::Info:
        background = p.primaryContainer;
        foreground = p.onPrimaryContainer;
        break;
    case Tone::Neutral:
    default:
        background = p.surfaceContainerHigh;
        foreground = p.onSurfaceVariant;
        break;
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 12, 12);

    const QFont font = uiFont(11, QFont::DemiBold);
    painter.setFont(font);
    const QFontMetricsF metrics(font);
    qreal x = 10;
    const qreal textWidth = metrics.horizontalAdvance(m_text);
    const qreal total = textWidth + (m_dot ? 11 : 0);
    x = (width() - total) / 2.0;
    if (m_dot) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_hasDotColor ? m_dotColor : foreground);
        painter.drawEllipse(QRectF(x, height() / 2.0 - 3, 6, 6));
        x += 11;
    }
    painter.setPen(foreground);
    painter.drawText(QRectF(x, 0, textWidth + 2, height()), Qt::AlignVCenter | Qt::AlignLeft,
                     m_text);
}

// ---------------------------------------------------------------------------
// M3FilterChip
// ---------------------------------------------------------------------------

M3FilterChip::M3FilterChip(const QString& text, QWidget* parent)
    : QAbstractButton(parent)
{
    setText(text);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedHeight(32);
    connect(this, &QAbstractButton::toggled, this, [this] { update(); });
}

QSize M3FilterChip::sizeHint() const
{
    const QFontMetricsF metrics(uiFont(12, QFont::DemiBold));
    return QSize(static_cast<int>(metrics.horizontalAdvance(text()) + 28), 32);
}

void M3FilterChip::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const Palette& p = pal();
    const QRectF bounds = rect().adjusted(0.5, 0.5, -0.5, -0.5);

    QColor background;
    QColor border;
    QColor foreground;
    if (isChecked()) {
        background = p.secondaryContainer;
        border = Qt::transparent;
        foreground = p.onSecondaryContainer;
    } else {
        background = m_hover ? alpha(p.onSurface, 14) : Qt::transparent;
        border = p.outlineVariant;
        foreground = p.onSurfaceVariant;
    }
    if (!isEnabled()) {
        background = alpha(p.onSurface, 10);
        border = p.outlineVariant;
        foreground = alpha(p.onSurface, 90);
    }

    painter.setPen(border.alpha() > 0 ? QPen(border, 1) : Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(bounds, 16, 16);

    QFont font = uiFont(12, isChecked() ? QFont::DemiBold : QFont::Normal);
    painter.setFont(font);
    painter.setPen(foreground);
    const QFontMetricsF metrics(font);
    painter.drawText(rect(), Qt::AlignCenter,
                     elided(metrics, text(), width() - 20));

    if (hasFocus()) {
        painter.setPen(QPen(alpha(p.primary, 150), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(bounds.adjusted(2, 2, -2, -2), 14, 14);
    }
}

void M3FilterChip::enterEvent(QEnterEvent* event)
{
    m_hover = true;
    update();
    QAbstractButton::enterEvent(event);
}

void M3FilterChip::leaveEvent(QEvent* event)
{
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(event);
}

// ---------------------------------------------------------------------------
// M3ProgressBar
// ---------------------------------------------------------------------------

M3ProgressBar::M3ProgressBar(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(m_barHeight + 2);
    m_animation = new QPropertyAnimation(this, "animatedValue", this);
    m_animation->setDuration(420);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
}

void M3ProgressBar::setValue(qreal value)
{
    m_value = qBound(0.0, value, 100.0);
    m_animation->stop();
    m_animation->setStartValue(m_animatedValue);
    m_animation->setEndValue(m_value);
    m_animation->start();
}

void M3ProgressBar::setAnimatedValue(qreal value)
{
    m_animatedValue = value;
    update();
}

void M3ProgressBar::setBarHeight(int height)
{
    m_barHeight = qMax(2, height);
    setFixedHeight(m_barHeight + 2);
    updateGeometry();
}

QSize M3ProgressBar::sizeHint() const
{
    return QSize(160, m_barHeight + 2);
}

void M3ProgressBar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const Palette& p = pal();
    const QRectF track(0, 1, width(), m_barHeight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(p.surfaceContainerHighest);
    painter.drawRoundedRect(track, m_barHeight / 2.0, m_barHeight / 2.0);

    const qreal fillWidth = track.width() * qBound(0.0, m_animatedValue, 100.0) / 100.0;
    if (fillWidth <= 0)
        return;
    QLinearGradient gradient(track.topLeft(), track.topRight());
    gradient.setColorAt(0.0, p.primary);
    gradient.setColorAt(1.0, p.secondary);
    painter.setBrush(gradient);
    painter.drawRoundedRect(QRectF(track.left(), track.top(), fillWidth, track.height()),
                            m_barHeight / 2.0, m_barHeight / 2.0);
}

// ---------------------------------------------------------------------------
// M3Divider
// ---------------------------------------------------------------------------

M3Divider::M3Divider(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(1);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize M3Divider::sizeHint() const
{
    return QSize(100, 1);
}

void M3Divider::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setPen(QPen(pal().outlineVariant, 1));
    painter.drawLine(0, 0, width(), 0);
}

// ---------------------------------------------------------------------------
// M3EmptyState
// ---------------------------------------------------------------------------

M3EmptyState::M3EmptyState(const QString& glyph, const QString& title, const QString& subtitle,
                           QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 40, 24, 40);
    layout->setSpacing(10);
    layout->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);

    m_icon = new M3IconLabel(glyph, this);
    m_icon->setFixedBox(74, 74);
    m_icon->setGlyphSize(32);
    m_icon->setBadge(true);
    layout->addWidget(m_icon, 0, Qt::AlignHCenter);

    m_title = new QLabel(title, this);
    m_title->setProperty("role", "section");
    m_title->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_title);

    m_subtitle = new QLabel(subtitle, this);
    m_subtitle->setProperty("role", "secondary");
    m_subtitle->setAlignment(Qt::AlignCenter);
    m_subtitle->setWordWrap(true);
    m_subtitle->setMaximumWidth(430);
    m_subtitle->setVisible(!subtitle.isEmpty());
    layout->addWidget(m_subtitle, 0, Qt::AlignHCenter);

    m_actionHost = new QWidget(this);
    auto* actionLayout = new QHBoxLayout(m_actionHost);
    actionLayout->setContentsMargins(0, 8, 0, 0);
    actionLayout->setSpacing(8);
    actionLayout->setAlignment(Qt::AlignCenter);
    m_actionHost->setVisible(false);
    layout->addWidget(m_actionHost);
}

QAbstractButton* M3EmptyState::addAction(const QString& text, M3Button::Variant variant)
{
    auto* button = new M3Button(text, variant, m_actionHost);
    if (auto* layout = qobject_cast<QHBoxLayout*>(m_actionHost->layout()))
        layout->addWidget(button);
    m_actionHost->setVisible(true);
    return button;
}

void M3EmptyState::setTitle(const QString& title)
{
    m_title->setText(title);
}

void M3EmptyState::setSubtitle(const QString& subtitle)
{
    m_subtitle->setText(subtitle);
    m_subtitle->setVisible(!subtitle.isEmpty());
}

// ---------------------------------------------------------------------------
// M3SettingRow
// ---------------------------------------------------------------------------

M3SettingRow::M3SettingRow(const QString& title, const QString& subtitle, QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(4, 9, 4, 9);
    root->setSpacing(13);

    m_icon = new M3IconLabel(QString(), this);
    m_icon->setFixedBox(38, 38);
    m_icon->setGlyphSize(20);
    m_icon->setVisible(false);
    root->addWidget(m_icon, 0, Qt::AlignVCenter);

    auto* texts = new QVBoxLayout;
    texts->setSpacing(2);
    texts->setContentsMargins(0, 0, 0, 0);
    m_title = new QLabel(title, this);
    m_title->setProperty("role", "bodyStrong");
    m_subtitle = new QLabel(subtitle, this);
    m_subtitle->setProperty("role", "caption");
    m_subtitle->setWordWrap(true);
    m_subtitle->setVisible(!subtitle.isEmpty());
    texts->addWidget(m_title);
    texts->addWidget(m_subtitle);
    root->addLayout(texts, 1);

    m_trailingHost = new QWidget(this);
    auto* trailingLayout = new QHBoxLayout(m_trailingHost);
    trailingLayout->setContentsMargins(0, 0, 0, 0);
    trailingLayout->setSpacing(8);
    root->addWidget(m_trailingHost, 0, Qt::AlignVCenter);
}

void M3SettingRow::setTitle(const QString& title)
{
    m_title->setText(title);
}

void M3SettingRow::setSubtitle(const QString& subtitle)
{
    m_subtitle->setText(subtitle);
    m_subtitle->setVisible(!subtitle.isEmpty());
}

void M3SettingRow::setLeadingIcon(const QString& glyph)
{
    m_icon->setGlyph(glyph);
    m_icon->setBadge(true);
    m_icon->clearColor();
    m_icon->setVisible(!glyph.isEmpty());
}

void M3SettingRow::setTrailing(QWidget* widget)
{
    if (m_trailing == widget)
        return;
    if (m_trailing) {
        m_trailing->setParent(nullptr);
        m_trailing->deleteLater();
    }
    m_trailing = widget;
    if (!widget)
        return;
    if (auto* layout = qobject_cast<QHBoxLayout*>(m_trailingHost->layout()))
        layout->addWidget(widget);
}

void M3SettingRow::setContentMargins(int left, int top, int right, int bottom)
{
    if (auto* layout = this->layout())
        layout->setContentsMargins(left, top, right, bottom);
}

// ---------------------------------------------------------------------------
// M3Snackbar
// ---------------------------------------------------------------------------

M3Snackbar::M3Snackbar(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_StyledBackground, false);
    setVisible(false);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 12, 20, 12);
    layout->setSpacing(11);
    m_icon = new M3IconLabel(QStringLiteral("info"), this);
    m_icon->setFixedBox(22, 22);
    m_icon->setGlyphSize(20);
    layout->addWidget(m_icon, 0, Qt::AlignVCenter);
    m_label = new QLabel(this);
    m_label->setProperty("role", "body");
    m_label->setWordWrap(true);
    m_label->setTextInteractionFlags(Qt::NoTextInteraction);
    layout->addWidget(m_label, 1);

    m_opacity = new QGraphicsOpacityEffect(this);
    m_opacity->setOpacity(1.0);
    setGraphicsEffect(m_opacity);

    m_positionAnimation = new QPropertyAnimation(this, "pos", this);
    m_positionAnimation->setDuration(190);
    m_positionAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_opacityAnimation = new QPropertyAnimation(m_opacity, "opacity", this);
    m_opacityAnimation->setDuration(170);
    m_opacityAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_opacityAnimation, &QPropertyAnimation::finished, this, [this] {
        if (m_opacity->opacity() < 0.05)
            hide();
    });

    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(4200);
    connect(&m_hideTimer, &QTimer::timeout, this, &M3Snackbar::hideAnimated);
}

void M3Snackbar::showMessage(const QString& message, bool error)
{
    m_error = error;
    m_label->setText(message);
    m_icon->setGlyph(error ? QStringLiteral("error_outline") : QStringLiteral("check_circle"));
    m_icon->setColor(error ? pal().error : pal().primary);

    QFontMetricsF metrics(Fonts::text(13));
    const int maxText = qMin(470, static_cast<int>(metrics.horizontalAdvance(message)) + 16);
    m_label->setFixedWidth(qMax(160, maxText));
    layout()->activate();
    setFixedWidth(qMin(590, maxText + 84));
    adjustSize();
    setFixedHeight(qMax(46, sizeHint().height()));

    QRect parentRect = parentWidget() ? parentWidget()->rect() : QRect(0, 0, 800, 600);
    int x = (parentRect.width() - width()) / 2;
    int targetY = parentRect.height() - height() - 28;
    x = qMax(16, x);
    targetY = qMax(16, targetY);

    m_hideTimer.stop();
    m_opacityAnimation->stop();
    m_positionAnimation->stop();
    show();
    raise();
    move(x, targetY + 14);
    m_opacity->setOpacity(0.0);
    m_opacityAnimation->setStartValue(0.0);
    m_opacityAnimation->setEndValue(1.0);
    m_opacityAnimation->start();
    m_positionAnimation->setStartValue(QPoint(x, targetY + 14));
    m_positionAnimation->setEndValue(QPoint(x, targetY));
    m_positionAnimation->start();
    m_hideTimer.start();
    update();
}

void M3Snackbar::reposition()
{
    if (!isVisible() || !parentWidget())
        return;
    QRect parentRect = parentWidget()->rect();
    const int x = qMax(16, (parentRect.width() - width()) / 2);
    const int y = qMax(16, parentRect.height() - height() - 28);
    move(x, y);
}

void M3Snackbar::hideAnimated()
{
    m_opacityAnimation->stop();
    m_positionAnimation->stop();
    m_opacityAnimation->setStartValue(m_opacity->opacity());
    m_opacityAnimation->setEndValue(0.0);
    m_opacityAnimation->start();
    m_positionAnimation->setStartValue(pos());
    m_positionAnimation->setEndValue(pos() + QPoint(0, 10));
    m_positionAnimation->start();
}

void M3Snackbar::dismissNow()
{
    m_hideTimer.stop();
    m_opacityAnimation->stop();
    m_positionAnimation->stop();
    hide();
}

void M3Snackbar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const Palette& p = pal();
    QColor background = p.surfaceContainerHighest;
    QColor border = p.outlineVariant;
    if (m_error) {
        background = Theme::blend(p.surfaceContainerHighest, p.error, 0.12);
        border = Theme::alpha(p.error, 110);
    }
    painter.setPen(QPen(border, 1));
    painter.setBrush(background);
    painter.drawRoundedRect(rect().adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
}
