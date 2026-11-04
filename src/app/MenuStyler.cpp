#include "app/MenuStyler.h"

#include "app/Theme.h"

#include <QApplication>
#include <QEvent>
#include <QMenu>

MenuStyler::MenuStyler(QObject* parent)
    : QObject(parent)
{
    connect(&Theme::instance(), &Theme::changed, this, &MenuStyler::applyToAll);
    applyToAll();
}

void MenuStyler::install()
{
    auto* styler = new MenuStyler(qApp);
    qApp->installEventFilter(styler);
}

void MenuStyler::styleMenu(QMenu* menu)
{
    if (!menu || menu->property("_clashqMaterialMenu").toBool())
        return;
    menu->setProperty("_clashqMaterialMenu", true);
    menu->setStyleSheet(Theme::instance().menuStyle());
    menu->update();
}

void MenuStyler::applyToAll()
{
    const auto widgets = QApplication::allWidgets();
    for (QWidget* widget : widgets) {
        if (auto* menu = qobject_cast<QMenu*>(widget))
            menu->setStyleSheet(Theme::instance().menuStyle());
    }
}

bool MenuStyler::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::Polish && event->type() != QEvent::Show)
        return false;
    if (auto* menu = qobject_cast<QMenu*>(watched))
        styleMenu(menu);
    return false;
}
