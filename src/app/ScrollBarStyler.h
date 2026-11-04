#pragma once

#include <QObject>

class QScrollBar;

// App-level QSS rules for QScrollBar are not reliably applied to scroll bars
// created by QAbstractScrollArea before the application style sheet is set.
// A local style sheet on each bar works consistently, so this filter applies
// the Material scrollbar style to every existing and newly polished bar.
class ScrollBarStyler final : public QObject {
public:
    static void install();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    explicit ScrollBarStyler(QObject* parent = nullptr);
    static void styleBar(QScrollBar* bar);
    static void applyToAll();
};
