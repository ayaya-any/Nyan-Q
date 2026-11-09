#pragma once

#include "scripts/ScriptTypes.h"
#include "ui/widgets/MaterialWidgets.h"

class ScriptCard : public M3Card {
    Q_OBJECT

public:
    explicit ScriptCard(const nq::scripts::ScriptMeta& script, QWidget* parent = nullptr);

signals:
    void editRequested(const QString& id);
    void deleteRequested(const QString& id);
    void enabledChanged(const QString& id, bool enabled);
    void moveRequested(const QString& id, int delta);
    void bindingsRequested(const QString& id);
    void updateRequested(const QString& id);
    void historyRequested(const QString& id);
    void logsRequested(const QString& id);
    void testRequested(const QString& id);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    nq::scripts::ScriptMeta m_script;
};
