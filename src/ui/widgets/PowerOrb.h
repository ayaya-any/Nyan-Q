#pragma once

#include "core/Types.h"

#include <QWidget>

class QVariantAnimation;

class PowerOrb : public QWidget {
    Q_OBJECT

public:
    explicit PowerOrb(QWidget* parent = nullptr);

    void setState(cq::CoreState state);
    cq::CoreState state() const { return m_state; }
    void setInteractive(bool interactive);
    QSize sizeHint() const override;

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    cq::CoreState m_state = cq::CoreState::Stopped;
    bool m_interactive = true;
    bool m_hovered = false;
    qreal m_phase = 0.0;
    QVariantAnimation* m_animation = nullptr;
};
