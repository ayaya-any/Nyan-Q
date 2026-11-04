#include "app/Fonts.h"

#include <QFontDatabase>
#include <QStringList>

namespace {
QString g_textFamily = QStringLiteral("Segoe UI");
QString g_iconFamily = QStringLiteral("Segoe UI Symbol");
bool g_loaded = false;

QString firstFamily(int id, const QString& fallback)
{
    if (id < 0)
        return fallback;
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    return families.isEmpty() ? fallback : families.first();
}
}  // namespace

namespace Fonts {

void load()
{
    if (g_loaded)
        return;
    g_loaded = true;

    const int roboto = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Roboto-Variable.ttf"));
    const int icons = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/MaterialIcons-Regular.ttf"));

    g_textFamily = firstFamily(roboto, QStringLiteral("Segoe UI"));
    g_iconFamily = firstFamily(icons, QStringLiteral("Segoe UI Symbol"));
}

QString textFamily()
{
    return g_textFamily;
}

QString iconFamily()
{
    return g_iconFamily;
}

QFont text(int pixelSize, QFont::Weight weight)
{
    QFont font;
    font.setFamilies({g_textFamily, QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Segoe UI")});
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

QFont icon(int pixelSize)
{
    QFont font(g_iconFamily);
    font.setPixelSize(pixelSize);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

QFont mono(int pixelSize, QFont::Weight weight)
{
    QFont font;
    font.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"),
                      QStringLiteral("JetBrains Mono"), QStringLiteral("Courier New")});
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    return font;
}

}  // namespace Fonts
