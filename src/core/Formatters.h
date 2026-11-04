#pragma once

#include "core/Types.h"

#include <QColor>
#include <QString>

namespace Formatters {

QString bytes(qint64 value, int decimals = -1);
QString rate(qint64 bytesPerSecond);
QString duration(qint64 seconds);
QString clockDuration(qint64 seconds);
QString dateTime(const QDateTime& value);
QString relativeTime(const QDateTime& value);
QString expiry(qint64 unixSeconds);
QString compactNumber(qint64 value);
QString endpoint(int port);
int delayQuality(int delayMs);
QColor delayColor(int delayMs);
QString percent(qint64 part, qint64 total);

}  // namespace Formatters
