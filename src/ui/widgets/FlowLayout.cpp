#include "ui/widgets/FlowLayout.h"

#include <QWidget>

FlowLayout::FlowLayout(QWidget* parent, int margin, int horizontalSpacing, int verticalSpacing)
    : QLayout(parent)
    , m_horizontalSpacing(horizontalSpacing)
    , m_verticalSpacing(verticalSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem* item = takeAt(0))
        delete item;
}

void FlowLayout::addItem(QLayoutItem* item)
{
    m_items.append(item);
}

int FlowLayout::count() const
{
    return m_items.size();
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return m_items.value(index);
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    if (index >= 0 && index < m_items.size())
        return m_items.takeAt(index);
    return nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    return {};
}

bool FlowLayout::hasHeightForWidth() const
{
    return true;
}

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, width, 0), true);
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const
{
    return minimumSize();
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem* item : m_items)
        size = size.expandedTo(item->minimumSize());
    const QMargins margins = contentsMargins();
    size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    return size;
}

int FlowLayout::horizontalSpacing() const
{
    if (m_horizontalSpacing >= 0)
        return m_horizontalSpacing;
    return smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int FlowLayout::verticalSpacing() const
{
    if (m_verticalSpacing >= 0)
        return m_verticalSpacing;
    return smartSpacing(QStyle::PM_LayoutVerticalSpacing);
}

void FlowLayout::setHorizontalSpacing(int spacing)
{
    m_horizontalSpacing = spacing;
    invalidate();
}

void FlowLayout::setVerticalSpacing(int spacing)
{
    m_verticalSpacing = spacing;
    invalidate();
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const
{
    const QMargins margins = contentsMargins();
    QRect effective = rect.marginsRemoved(margins);
    int x = effective.x();
    int y = effective.y();
    int lineHeight = 0;

    for (QLayoutItem* item : m_items) {
        const QSize hint = item->sizeHint();
        int nextX = x + hint.width() + horizontalSpacing();
        if (nextX - horizontalSpacing() > effective.right() && lineHeight > 0) {
            x = effective.x();
            y += lineHeight + verticalSpacing();
            nextX = x + hint.width() + horizontalSpacing();
            lineHeight = 0;
        }
        if (!testOnly)
            item->setGeometry(QRect(QPoint(x, y), hint));
        x = nextX;
        lineHeight = qMax(lineHeight, hint.height());
    }
    return y + lineHeight - rect.y() + margins.bottom();
}

int FlowLayout::smartSpacing(QStyle::PixelMetric pm) const
{
    QObject* parent = this->parent();
    if (!parent)
        return -1;
    if (parent->isWidgetType()) {
        auto* widget = static_cast<QWidget*>(parent);
        return widget->style()->pixelMetric(pm, nullptr, widget);
    }
    return static_cast<QLayout*>(parent)->spacing();
}
