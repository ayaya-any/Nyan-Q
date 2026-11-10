#pragma once

#include "ui/dialogs/MaterialDialog.h"

class MessageDialog final : public MaterialDialog {
    Q_OBJECT

public:
    explicit MessageDialog(QWidget* parent, const QString& title, const QString& message,
                           bool warning = false);

    static void showMessage(QWidget* parent, const QString& title, const QString& message,
                     bool warning = false);
};
