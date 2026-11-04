#pragma once

#include "core/Types.h"
#include "ui/widgets/MaterialWidgets.h"

class ProfileCard : public M3Card {
    Q_OBJECT

public:
    explicit ProfileCard(const cq::ProfileMeta& profile, QWidget* parent = nullptr);

signals:
    void activateRequested(const QString& id);
    void updateRequested(const QString& id);
    void editRequested(const QString& id);
    void deleteRequested(const QString& id);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    cq::ProfileMeta m_profile;
};
