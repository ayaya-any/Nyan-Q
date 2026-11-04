#pragma once

#include "core/Types.h"

#include <QVector>
#include <QWidget>

class ClashController;
class M3Button;
class M3IconButton;
class M3SegmentedControl;
class M3Switch;
class PageHeader;
class QLabel;
class QPlainTextEdit;

class LogsPage : public QWidget {
    Q_OBJECT

public:
    explicit LogsPage(ClashController* controller, QWidget* parent = nullptr);

    void refresh();

private:
    void buildUi();
    void wire();
    void appendEntry(const cq::LogEntry& entry);
    void rebuild();
    bool accepts(cq::LogLevel level) const;
    void updateCount();

    ClashController* m_controller = nullptr;
    PageHeader* m_header = nullptr;
    M3SegmentedControl* m_levelFilter = nullptr;
    M3Switch* m_autoScroll = nullptr;
    QLabel* m_countLabel = nullptr;
    QPlainTextEdit* m_view = nullptr;
    QVector<cq::LogEntry> m_entries;
    bool m_bulkUpdating = false;
};
