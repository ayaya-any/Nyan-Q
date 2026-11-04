#include "app/RoundedPopupFilter.h"

#include <QEvent>
#include <QPainterPath>
#include <QRegion>
#include <QWidget>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>
#endif

RoundedPopupFilter::RoundedPopupFilter(QObject* parent)
    : QObject(parent)
{
}

void RoundedPopupFilter::applyRoundedWindow(QWidget* popup, int fallbackRadius)
{
    if (!popup || popup->size().isEmpty())
        return;

#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(popup->winId());
    if (hwnd) {
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
        const int preference = 2; // DWMWCP_ROUND
        const HRESULT result = DwmSetWindowAttribute(
            hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
        if (SUCCEEDED(result))
            return;
    }
#endif

    // Pre-Windows 11 fallback: clip the native popup with a rounded region.
    QPainterPath path;
    path.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(popup->size())), fallbackRadius,
                        fallbackRadius);
    popup->setMask(QRegion(path.toFillPolygon().toPolygon()));
}

bool RoundedPopupFilter::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::Show && event->type() != QEvent::Resize)
        return false;

    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget)
        return false;

    if (widget->inherits("QTipLabel") || widget->inherits("QMenu"))
        applyRoundedWindow(widget, 8);

    return false;
}
