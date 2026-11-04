#pragma once

#include <QWidget>

class QLineEdit;
class M3IconButton;
class M3IconLabel;

class SearchField : public QWidget {
    Q_OBJECT

public:
    explicit SearchField(const QString& placeholder = QStringLiteral("搜索"), QWidget* parent = nullptr);

    QString text() const;
    void setPlaceholder(const QString& placeholder);
    void setText(const QString& text);
    void clear();
    QLineEdit* lineEdit() const { return m_edit; }

signals:
    void textChanged(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateClearButton();

    QLineEdit* m_edit = nullptr;
    M3IconLabel* m_icon = nullptr;
    M3IconButton* m_clear = nullptr;
    bool m_focused = false;
};
