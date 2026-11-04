#include "ui/widgets/TrafficChart.h"

#include "app/Fonts.h"
#include "app/Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>

#include <algorithm>
#include <cmath>

namespace {
QPainterPath smoothPath(const QVector<QPointF>& points)
{
    QPainterPath path;
    if (points.isEmpty())
        return path;
    path.moveTo(points.first());
    if (points.size() == 1) {
        path.lineTo(points.first());
        return path;
    }
    for (int i = 1; i < points.size() - 1; ++i) {
        const QPointF mid = (points.at(i) + points.at(i + 1)) / 2.0;
        path.quadTo(points.at(i), mid);
    }
    path.lineTo(points.last());
    return path;
}
}  // namespace

TrafficChart::TrafficChart(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void TrafficChart::addSample(qint64 download, qint64 upload)
{
    m_download.append(qMax<qint64>(0, download));
    m_upload.append(qMax<qint64>(0, upload));
    while (m_download.size() > m_maxSamples)
        m_download.removeFirst();
    while (m_upload.size() > m_maxSamples)
        m_upload.removeFirst();
    update();
}

void TrafficChart::clear()
{
    m_download.clear();
    m_upload.clear();
    update();
}

void TrafficChart::setMaxSamples(int samples)
{
    m_maxSamples = qMax(10, samples);
}

void TrafficChart::setCaption(const QString& caption)
{
    m_caption = caption;
    update();
}

void TrafficChart::setScaleFromZero(bool fromZero)
{
    m_scaleFromZero = fromZero;
    update();
}

QSize TrafficChart::sizeHint() const
{
    return QSize(520, 174);
}

void TrafficChart::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const Palette& p = pal();

    const QRectF area = rect().adjusted(2, 8, -2, -2);
    if (area.width() <= 4 || area.height() <= 4)
        return;

    const int horizontalLines = 4;
    QPen gridPen(Theme::alpha(p.outlineVariant, 150), 1, Qt::DashLine);
    gridPen.setDashPattern({2.0, 6.0});
    painter.setPen(gridPen);
    for (int i = 0; i <= horizontalLines; ++i) {
        const qreal y = area.top() + area.height() * i / horizontalLines;
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }

    if (m_download.isEmpty() && m_upload.isEmpty())
        return;

    const int count = std::max(m_download.size(), m_upload.size());
    const int samples = std::max(count, 2);
    qint64 maximum = 64LL * 1024;
    qint64 minimum = 0;
    for (qint64 value : m_download)
        maximum = std::max(maximum, value);
    for (qint64 value : m_upload)
        maximum = std::max(maximum, value);

    if (!m_scaleFromZero) {
        minimum = maximum;
        for (qint64 value : m_download)
            minimum = std::min(minimum, value);
        for (qint64 value : m_upload)
            minimum = std::min(minimum, value);
        const qint64 span = std::max<qint64>(1, maximum - minimum);
        const qint64 padding = std::max<qint64>(1, span / 8);
        minimum = std::max<qint64>(0, minimum - padding);
        maximum += padding;
    }

    const qreal scaleMax = static_cast<qreal>(maximum) * (m_scaleFromZero ? 1.18 : 1.0);
    const qreal scaleSpan =
        qMax<qreal>(1.0, static_cast<qreal>(maximum - minimum));

    auto buildPoints = [&](const QVector<qint64>& values) {
        QVector<QPointF> points;
        points.reserve(samples);
        const int missing = samples - values.size();
        for (int i = 0; i < samples; ++i) {
            const qreal x = area.left() + area.width() * i / (samples - 1);
            const qint64 value = (i < missing) ? 0 : values.at(i - missing);
            const qreal normalized = m_scaleFromZero
                                         ? qBound(0.0, static_cast<qreal>(value) / scaleMax, 1.0)
                                         : qBound(0.0, (static_cast<qreal>(value) - minimum) /
                                                           scaleSpan,
                                                  1.0);
            const qreal y = area.bottom() - normalized * area.height() * 0.92;
            points.append(QPointF(x, y));
        }
        return points;
    };

    auto drawSeries = [&](const QVector<qint64>& values, const QColor& color) {
        const QVector<QPointF> points = buildPoints(values);
        if (points.isEmpty())
            return;
        QPainterPath line = smoothPath(points);
        QPainterPath fill = line;
        fill.lineTo(area.right(), area.bottom());
        fill.lineTo(area.left(), area.bottom());
        fill.closeSubpath();

        QLinearGradient gradient(0, area.top(), 0, area.bottom());
        gradient.setColorAt(0.0, Theme::alpha(color, 70));
        gradient.setColorAt(0.75, Theme::alpha(color, 16));
        gradient.setColorAt(1.0, Theme::alpha(color, 0));
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawPath(fill);

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(line);
        painter.restore();
    };

    drawSeries(m_upload, p.chartUpload);
    drawSeries(m_download, p.chartDownload);

    if (!m_caption.isEmpty()) {
        painter.setFont(Fonts::text(10));
        painter.setPen(Theme::alpha(p.onSurfaceVariant, 180));
        painter.drawText(QRectF(area.left() + 4, area.top() - 2, 120, 14),
                         Qt::AlignLeft | Qt::AlignVCenter, m_caption);
    }
}
