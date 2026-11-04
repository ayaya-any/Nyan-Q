#include "ui/dialogs/MaterialDialog.h"

#include "app/Fonts.h"
#include "app/Theme.h"
#include "ui/widgets/MaterialWidgets.h"

#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QWindow>

MaterialDialog::MaterialDialog(QWidget* parent, const QString& title, const QString& subtitle)
    : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setModal(true);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(28, 28, 28, 28);
    outer->setSpacing(0);
    outer->addStretch(1);

    auto* center = new QHBoxLayout;
    center->setContentsMargins(0, 0, 0, 0);
    center->addStretch(1);

    m_surface = new M3Card(this, 24);
    m_surface->setFixedWidth(m_width);
    auto* shadow = new QGraphicsDropShadowEffect(m_surface);
    shadow->setBlurRadius(42);
    shadow->setOffset(0, 14);
    shadow->setColor(QColor(0, 0, 0, 130));
    m_surface->setGraphicsEffect(shadow);
    center->addWidget(m_surface, 0, Qt::AlignCenter);
    center->addStretch(1);
    outer->addLayout(center);
    outer->addStretch(1);

    auto* card = new QVBoxLayout(m_surface);
    card->setContentsMargins(26, 22, 24, 20);
    card->setSpacing(0);

    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(12);
    auto* titles = new QVBoxLayout;
    titles->setContentsMargins(0, 0, 0, 0);
    titles->setSpacing(3);
    m_titleLabel = new QLabel(title, m_surface);
    m_titleLabel->setFont(Fonts::text(20, QFont::DemiBold));
    titles->addWidget(m_titleLabel);
    m_subtitleLabel = new QLabel(subtitle, m_surface);
    m_subtitleLabel->setProperty("role", "secondary");
    m_subtitleLabel->setWordWrap(true);
    m_subtitleLabel->setVisible(!subtitle.isEmpty());
    titles->addWidget(m_subtitleLabel);
    header->addLayout(titles, 1);

    m_close = new M3IconButton(QStringLiteral("close"), m_surface);
    m_close->setDiameter(32);
    m_close->setGlyphSize(17);
    m_close->setToolTip(QStringLiteral("关闭"));
    header->addWidget(m_close, 0, Qt::AlignTop);
    card->addLayout(header);
    card->addSpacing(18);

    m_contentLayout = new QVBoxLayout;
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(10);
    card->addLayout(m_contentLayout);

    m_errorLabel = new QLabel(m_surface);
    m_errorLabel->setProperty("role", "error");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);
    m_contentLayout->addWidget(m_errorLabel);

    card->addSpacing(22);
    auto* footerLine = new M3Divider(m_surface);
    card->addWidget(footerLine);
    card->addSpacing(16);

    m_footerLayout = new QHBoxLayout;
    m_footerLayout->setContentsMargins(0, 0, 0, 0);
    m_footerLayout->setSpacing(8);
    m_footerLayout->setAlignment(Qt::AlignRight);
    card->addLayout(m_footerLayout);

    connect(m_close, &QAbstractButton::clicked, this, [this] { requestClose(); });
}

MaterialDialog::~MaterialDialog() = default;

void MaterialDialog::setDialogWidth(int width)
{
    m_width = width;
    m_surface->setFixedWidth(width);
}

void MaterialDialog::setPrimaryAction(M3Button* button)
{
    m_primary = button;
}

void MaterialDialog::setSubtitle(const QString& text)
{
    m_subtitleLabel->setText(text);
    m_subtitleLabel->setVisible(!text.isEmpty());
}

void MaterialDialog::setErrorText(const QString& text)
{
    m_errorLabel->setText(text);
    m_errorLabel->setVisible(!text.isEmpty());
}

void MaterialDialog::clearError()
{
    m_errorLabel->clear();
    m_errorLabel->setVisible(false);
}

void MaterialDialog::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), pal().backdrop);
}

void MaterialDialog::showEvent(QShowEvent* event)
{
    positionOverParent();
    QDialog::showEvent(event);
}

void MaterialDialog::positionOverParent()
{
    QWidget* parentWindow = parentWidget() ? parentWidget()->window() : nullptr;
    if (!parentWindow)
        return;
    const QPoint globalTopLeft = parentWindow->mapToGlobal(QPoint(0, 0));
    setGeometry(QRect(globalTopLeft, parentWindow->size()));
}

void MaterialDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        requestClose();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && m_primary) {
        m_primary->click();
        return;
    }
    QDialog::keyPressEvent(event);
}

void MaterialDialog::requestClose()
{
    reject();
}
