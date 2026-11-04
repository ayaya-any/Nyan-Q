#pragma once

#include "core/Types.h"

#include <QWidget>

class QAbstractButton;
class QButtonGroup;
class QLabel;
class M3Card;
class M3IconLabel;

class Sidebar : public QWidget {
    Q_OBJECT

public:
    explicit Sidebar(QWidget* parent = nullptr);

    void setCurrentPage(int index);
    void setCoreState(cq::CoreState state);
    void setCoreVersion(const QString& version);
    void setActiveProfileName(const QString& name);
    void setStats(const cq::SessionStats& stats);
    int currentPage() const;
    QSize sizeHint() const override;

signals:
    void pageSelected(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QAbstractButton* addNavItem(QButtonGroup* group, int index, const QString& glyph,
                               const QString& text);
    void updateProfileCard();

    QButtonGroup* m_navGroup = nullptr;
    QList<QAbstractButton*> m_navItems;
    M3Card* m_profileCard = nullptr;
    M3IconLabel* m_stateDot = nullptr;
    QLabel* m_stateLabel = nullptr;
    QLabel* m_profileName = nullptr;
    QLabel* m_sessionLabel = nullptr;
    QLabel* m_versionLabel = nullptr;
    cq::CoreState m_state = cq::CoreState::Stopped;
    cq::SessionStats m_stats;
    QString m_coreVersion;
};
