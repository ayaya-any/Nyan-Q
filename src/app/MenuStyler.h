#pragma once

#include <QObject>

class QMenu;

// QMenu instances created before the application style sheet is applied can
// keep the native Fusion menu painting. Apply the Material menu style locally
// so the tray menu is styled consistently and follows theme switches.
class MenuStyler final : public QObject {
public:
    static void install();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    explicit MenuStyler(QObject* parent = nullptr);
    static void styleMenu(QMenu* menu);
    static void applyToAll();
};
