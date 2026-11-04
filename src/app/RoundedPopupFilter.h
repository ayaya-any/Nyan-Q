#pragma once

#include <QObject>

class QWidget;

// Qt stylesheet border-radius only affects a popup's painted contents, not the
// native top-level window shape. QToolTip and QMenu therefore keep opaque
// square corners around their rounded borders. This filter asks DWM to round
// those native popup windows on Windows 11 so the corners stay antialiased.
class RoundedPopupFilter final : public QObject {
public:
    explicit RoundedPopupFilter(QObject* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    static void applyRoundedWindow(QWidget* popup, int fallbackRadius);
};
