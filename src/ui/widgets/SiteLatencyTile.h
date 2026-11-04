#pragma once

#include "core/Types.h"

#include <QWidget>

class SiteLatencyTile final : public QWidget {
    Q_OBJECT

public:
    explicit SiteLatencyTile(const cq::SiteLatency& site, QWidget* parent = nullptr);

    void setSite(const cq::SiteLatency& site);
    const cq::SiteLatency& site() const { return m_site; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString badgeText() const;

    cq::SiteLatency m_site;
};
