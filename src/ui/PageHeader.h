#pragma once

#include <QWidget>

class QHBoxLayout;
class QLabel;

class PageHeader : public QWidget {
    Q_OBJECT

public:
    explicit PageHeader(const QString& title, const QString& subtitle = {},
                        QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setSubtitle(const QString& subtitle);
    QString title() const;
    QHBoxLayout* actionLayout() const { return m_actions; }
    void addAction(QWidget* widget);

private:
    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    QHBoxLayout* m_actions = nullptr;
};
