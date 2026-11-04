#pragma once

#include "core/Types.h"

#include <QWidget>

class ProxyNodeCard : public QWidget {
    Q_OBJECT

public:
    explicit ProxyNodeCard(const cq::ProxyNode& node, bool selected, QWidget* parent = nullptr);

    const QString& nodeName() const { return m_node.name; }
    void setNode(const cq::ProxyNode& node, bool selected);
    void setDelay(int delay);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void selected(const QString& node);
    void testRequested(const QString& node);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void refreshToolTip();

    cq::ProxyNode m_node;
    bool m_selected = false;
    bool m_hovered = false;
};
