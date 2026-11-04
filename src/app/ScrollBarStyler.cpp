#include "app/ScrollBarStyler.h"

#include "app/Theme.h"

#include <QApplication>
#include <QEvent>
#include <QScrollBar>
#include <QWidget>

ScrollBarStyler::ScrollBarStyler(QObject* parent)
    : QObject(parent)
{
    connect(&Theme::instance(), &Theme::changed, this, &ScrollBarStyler::applyToAll);
    applyToAll();
}

void ScrollBarStyler::install()
{
    auto* styler = new ScrollBarStyler(qApp);
    qApp->installEventFilter(styler);
}

void ScrollBarStyler::styleBar(QScrollBar* bar)
{
    if (!bar || bar->property("_nyanqMaterialScroll").toBool())
        return;
    bar->setProperty("_nyanqMaterialScroll", true);
    bar->setStyleSheet(Theme::instance().scrollBarStyle());
    bar->update();
}

void ScrollBarStyler::applyToAll()
{
    const auto widgets = QApplication::allWidgets();
    for (QWidget* widget : widgets) {
        if (auto* bar = qobject_cast<QScrollBar*>(widget))
            bar->setStyleSheet(Theme::instance().scrollBarStyle());
    }
}

bool ScrollBarStyler::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::Polish && event->type() != QEvent::Show)
        return false;
    if (auto* bar = qobject_cast<QScrollBar*>(watched))
        styleBar(bar);
    return false;
}
