#include "ui/widgets/SearchField.h"

#include "app/Fonts.h"
#include "app/Theme.h"
#include "ui/widgets/MaterialWidgets.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>

SearchField::SearchField(const QString& placeholder, QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(38);
    setMinimumWidth(190);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setCursor(Qt::IBeamCursor);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 6, 0);
    layout->setSpacing(7);

    m_icon = new M3IconLabel(QStringLiteral("search"), this);
    m_icon->setFixedBox(18, 18);
    m_icon->setGlyphSize(18);
    layout->addWidget(m_icon, 0, Qt::AlignVCenter);

    m_edit = new QLineEdit(this);
    m_edit->setProperty("variant", "embedded");
    m_edit->setPlaceholderText(placeholder);
    m_edit->setFont(Fonts::text(13));
    m_edit->setClearButtonEnabled(false);
    m_edit->installEventFilter(this);
    layout->addWidget(m_edit, 1);

    m_clear = new M3IconButton(QStringLiteral("close"), this);
    m_clear->setDiameter(26);
    m_clear->setGlyphSize(15);
    m_clear->setToolTip(QStringLiteral("清空"));
    m_clear->setVisible(false);
    layout->addWidget(m_clear, 0, Qt::AlignVCenter);

    setFocusProxy(m_edit);

    connect(m_edit, &QLineEdit::textChanged, this, [this](const QString& text) {
        updateClearButton();
        emit textChanged(text);
    });
    connect(m_clear, &QAbstractButton::clicked, this, [this] {
        m_edit->clear();
        m_edit->setFocus();
    });
}

QString SearchField::text() const
{
    return m_edit ? m_edit->text() : QString();
}

void SearchField::setPlaceholder(const QString& placeholder)
{
    m_edit->setPlaceholderText(placeholder);
}

void SearchField::setText(const QString& text)
{
    m_edit->setText(text);
}

void SearchField::clear()
{
    m_edit->clear();
}

void SearchField::updateClearButton()
{
    if (m_clear)
        m_clear->setVisible(!m_edit->text().isEmpty());
}

void SearchField::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const Palette& p = pal();
    QColor background = m_focused ? p.surfaceContainerHigh : p.surfaceContainer;
    QColor border = m_focused ? p.primary : p.outlineVariant;
    painter.setPen(QPen(border, 1));
    painter.setBrush(background);
    painter.drawRoundedRect(rect().adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
}

void SearchField::mousePressEvent(QMouseEvent* event)
{
    m_edit->setFocus();
    QWidget::mousePressEvent(event);
}

bool SearchField::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_edit) {
        if (event->type() == QEvent::FocusIn) {
            m_focused = true;
            update();
        } else if (event->type() == QEvent::FocusOut) {
            m_focused = false;
            update();
        }
    }
    return QWidget::eventFilter(watched, event);
}
