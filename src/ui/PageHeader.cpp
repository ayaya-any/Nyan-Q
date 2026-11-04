#include "ui/PageHeader.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

PageHeader::PageHeader(const QString& title, const QString& subtitle, QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(16);

    auto* text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(4);

    m_title = new QLabel(title, this);
    m_title->setProperty("role", "title");
    text->addWidget(m_title);

    m_subtitle = new QLabel(subtitle, this);
    m_subtitle->setProperty("role", "secondary");
    m_subtitle->setWordWrap(true);
    m_subtitle->setVisible(!subtitle.isEmpty());
    text->addWidget(m_subtitle);

    root->addLayout(text, 1);

    m_actions = new QHBoxLayout;
    m_actions->setContentsMargins(0, 0, 0, 0);
    m_actions->setSpacing(8);
    m_actions->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    root->addLayout(m_actions, 0);
}

void PageHeader::setTitle(const QString& title)
{
    m_title->setText(title);
}

void PageHeader::setSubtitle(const QString& subtitle)
{
    m_subtitle->setText(subtitle);
    m_subtitle->setVisible(!subtitle.isEmpty());
}

QString PageHeader::title() const
{
    return m_title->text();
}

void PageHeader::addAction(QWidget* widget)
{
    if (widget)
        m_actions->addWidget(widget, 0, Qt::AlignVCenter);
}
