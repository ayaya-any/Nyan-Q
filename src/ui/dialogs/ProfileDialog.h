#pragma once

#include "ui/dialogs/MaterialDialog.h"

#include <QUrl>

class QLineEdit;

class ProfileDialog : public MaterialDialog {
    Q_OBJECT

public:
    explicit ProfileDialog(QWidget* parent = nullptr);

    QString profileName() const;
    QUrl profileUrl() const;

private:
    void tryAccept();
    void validate(const QString& text);

    QLineEdit* m_name = nullptr;
    QLineEdit* m_url = nullptr;
};
