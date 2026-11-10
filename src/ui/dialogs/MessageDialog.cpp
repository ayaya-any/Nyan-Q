#include "ui/dialogs/MessageDialog.h"

#include "app/Theme.h"
#include "ui/widgets/MaterialWidgets.h"

#include <QHBoxLayout>
#include <QLabel>

MessageDialog::MessageDialog(QWidget* parent, const QString& title, const QString& message,
                             bool warning)
    : MaterialDialog(parent, title, {})
{
    setDialogWidth(440);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(14);

    auto* icon = new M3IconLabel(warning ? QStringLiteral("warning") : QStringLiteral("info"),
                                 this);
    icon->setFixedBox(42, 42);
    icon->setGlyphSize(24);
    icon->setBadge(true);
    icon->setColor(warning ? pal().warning : pal().primary);
    row->addWidget(icon, 0, Qt::AlignTop);

    auto* text = new QLabel(message, this);
    text->setProperty("role", "body");
    text->setWordWrap(true);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(text, 1);
    contentLayout()->addLayout(row);

    auto* ok = new M3Button(QStringLiteral("确定"), M3Button::Variant::Filled, this);
    footerLayout()->addWidget(ok);
    setPrimaryAction(ok);
    connect(ok, &QAbstractButton::clicked, this, &QDialog::accept);
}

void MessageDialog::showMessage(QWidget* parent, const QString& title, const QString& message,
                         bool warning)
{
    MessageDialog dialog(parent, title, message, warning);
    dialog.exec();
}
