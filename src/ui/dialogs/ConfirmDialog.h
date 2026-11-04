#pragma once

#include "ui/dialogs/MaterialDialog.h"

class ConfirmDialog final : public MaterialDialog {
    Q_OBJECT

public:
    explicit ConfirmDialog(QWidget* parent, const QString& title, const QString& message,
                           const QString& confirmText, bool danger = true);

    static bool ask(QWidget* parent, const QString& title, const QString& message,
                    const QString& confirmText, bool danger = true);
};
