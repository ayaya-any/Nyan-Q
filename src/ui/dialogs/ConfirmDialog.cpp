#include "ui/dialogs/ConfirmDialog.h"

#include "app/Fonts.h"
#include "app/Theme.h"
#include "ui/widgets/MaterialWidgets.h"

#include <QHBoxLayout>
#include <QLabel>

ConfirmDialog::ConfirmDialog(QWidget* parent, const QString& title, const QString& message,
                             const QString& confirmText, bool danger)
    : MaterialDialog(parent, title, {})
{
    setDialogWidth(460);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(14);

    auto* icon = new M3IconLabel(danger ? QStringLiteral("warning") : QStringLiteral("help"),
                                 this);
    icon->setFixedBox(42, 42);
    icon->setGlyphSize(24);
    icon->setBadge(true);
    if (danger)
        icon->setColor(pal().error);
    row->addWidget(icon, 0, Qt::AlignTop);

    auto* text = new QLabel(message, this);
    text->setProperty("role", "body");
    text->setWordWrap(true);
    row->addWidget(text, 1);
    contentLayout()->addLayout(row);

    auto* cancel = new M3Button(QStringLiteral("取消"), M3Button::Variant::Text, this);
    auto* confirm = new M3Button(confirmText,
                                 danger ? M3Button::Variant::Danger : M3Button::Variant::Filled,
                                 this);
    footerLayout()->addWidget(cancel);
    footerLayout()->addWidget(confirm);
    setPrimaryAction(confirm);

    connect(cancel, &QAbstractButton::clicked, this, &QDialog::reject);
    connect(confirm, &QAbstractButton::clicked, this, &QDialog::accept);
}

bool ConfirmDialog::ask(QWidget* parent, const QString& title, const QString& message,
                        const QString& confirmText, bool danger)
{
    ConfirmDialog dialog(parent, title, message, confirmText, danger);
    return dialog.exec() == QDialog::Accepted;
}
