#include "ui/TitleBar.h"

#include "app/Fonts.h"
#include "app/Theme.h"
#include "ui/widgets/MaterialWidgets.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWindow>

TitleBar::TitleBar(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(50);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(20, 0, 10, 0);
    layout->setSpacing(10);

    auto* brandMark = new QLabel(this);
    brandMark->setPixmap(QIcon(QStringLiteral(":/icons/app.png")).pixmap(28, 28));
    brandMark->setFixedSize(30, 30);
    brandMark->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    layout->addWidget(brandMark, 0, Qt::AlignVCenter);

    auto* brand = new QLabel(QStringLiteral("Nyan-Q"), this);
    QFont brandFont = Fonts::text(15, QFont::Bold);
    brand->setFont(brandFont);
    brand->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    layout->addWidget(brand, 0, Qt::AlignVCenter);

    layout->addStretch(1);

    m_minimize = new M3IconButton(QStringLiteral("remove"), this);
    m_minimize->setDiameter(34);
    m_minimize->setGlyphSize(18);
    m_minimize->setToolTip(QStringLiteral("最小化"));
    layout->addWidget(m_minimize, 0, Qt::AlignVCenter);

    m_maximize = new M3IconButton(QStringLiteral("crop_square"), this);
    m_maximize->setDiameter(34);
    m_maximize->setGlyphSize(16);
    m_maximize->setToolTip(QStringLiteral("最大化"));
    layout->addWidget(m_maximize, 0, Qt::AlignVCenter);

    m_close = new M3IconButton(QStringLiteral("close"), this);
    m_close->setDiameter(34);
    m_close->setGlyphSize(18);
    m_close->setDangerOnHover(true);
    m_close->setToolTip(QStringLiteral("关闭"));
    layout->addWidget(m_close, 0, Qt::AlignVCenter);

    connect(m_minimize, &QAbstractButton::clicked, this, &TitleBar::minimizeRequested);
    connect(m_maximize, &QAbstractButton::clicked, this, &TitleBar::maximizeRequested);
    connect(m_close, &QAbstractButton::clicked, this, &TitleBar::closeRequested);
}

void TitleBar::setMaximized(bool maximized)
{
    m_maximize->setGlyph(maximized ? QStringLiteral("filter_none")
                                   : QStringLiteral("crop_square"));
    m_maximize->setToolTip(maximized ? QStringLiteral("还原") : QStringLiteral("最大化"));
}

void TitleBar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), pal().surface);
    painter.setPen(QPen(pal().outlineVariant, 1));
    painter.drawLine(0, height() - 1, width(), height() - 1);
}

void TitleBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (QWindow* handle = window()->windowHandle())
            handle->startSystemMove();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit maximizeRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TitleBar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
}
