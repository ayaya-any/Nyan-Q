#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class TrafficChart : public QWidget {
    Q_OBJECT

public:
    explicit TrafficChart(QWidget* parent = nullptr);

    void addSample(qint64 download, qint64 upload);
    void clear();
    void setMaxSamples(int samples);
    void setCaption(const QString& caption);
    void setScaleFromZero(bool fromZero);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<qint64> m_download;
    QVector<qint64> m_upload;
    QString m_caption = QStringLiteral("实时速率");
    int m_maxSamples = 60;
    bool m_scaleFromZero = true;
};
