#pragma once

#include <QLayout>
#include <QList>
#include <QRect>
#include <QSize>
#include <QStyle>

class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent = nullptr, int margin = 0, int horizontalSpacing = 12,
                        int verticalSpacing = 12);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;

    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    void setGeometry(const QRect& rect) override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;

    int horizontalSpacing() const;
    int verticalSpacing() const;
    void setHorizontalSpacing(int spacing);
    void setVerticalSpacing(int spacing);

private:
    int doLayout(const QRect& rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem*> m_items;
    int m_horizontalSpacing;
    int m_verticalSpacing;
};
