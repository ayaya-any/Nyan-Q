#pragma once

#include <QWidget>

class M3IconButton;

class TitleBar : public QWidget {
    Q_OBJECT

public:
    explicit TitleBar(QWidget* parent = nullptr);

    void setMaximized(bool maximized);

signals:
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    M3IconButton* m_minimize = nullptr;
    M3IconButton* m_maximize = nullptr;
    M3IconButton* m_close = nullptr;
};
