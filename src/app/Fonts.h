#pragma once

#include <QFont>
#include <QString>

namespace Fonts {

void load();
QString textFamily();
QString iconFamily();
QFont text(int pixelSize, QFont::Weight weight = QFont::Normal);
QFont icon(int pixelSize);
QFont mono(int pixelSize, QFont::Weight weight = QFont::Normal);

}  // namespace Fonts
