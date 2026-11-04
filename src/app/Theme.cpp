#include "app/Theme.h"

#include "app/Fonts.h"

#include <QApplication>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyleHints>

namespace {
Palette makeDark()
{
    Palette p;
    p.dark = true;
    p.background = QColor(QStringLiteral("#1E1E1E"));
    p.surface = QColor(QStringLiteral("#1C1C1C"));
    p.surfaceContainer = QColor(QStringLiteral("#252526"));
    p.surfaceContainerHigh = QColor(QStringLiteral("#2D2D30"));
    p.surfaceContainerHighest = QColor(QStringLiteral("#333337"));
    p.onSurface = QColor(QStringLiteral("#F1F1F1"));
    p.onSurfaceVariant = QColor(QStringLiteral("#C8C8C8"));
    p.outline = QColor(QStringLiteral("#5A5A5A"));
    p.outlineVariant = QColor(QStringLiteral("#3A3A3A"));
    p.primary = QColor(QStringLiteral("#5CA8FF"));
    p.onPrimary = QColor(QStringLiteral("#061E36"));
    p.primaryContainer = QColor(QStringLiteral("#173B5C"));
    p.onPrimaryContainer = QColor(QStringLiteral("#D8EAFF"));
    p.secondary = QColor(QStringLiteral("#9EC5E2"));
    p.onSecondary = QColor(QStringLiteral("#0B2A3A"));
    p.secondaryContainer = QColor(QStringLiteral("#243A4A"));
    p.onSecondaryContainer = QColor(QStringLiteral("#D6EBF8"));
    p.tertiary = QColor(QStringLiteral("#E8B36A"));
    p.error = QColor(QStringLiteral("#F1707A"));
    p.onError = QColor(QStringLiteral("#3B0007"));
    p.success = QColor(QStringLiteral("#6CCB9A"));
    p.warning = QColor(QStringLiteral("#E8C06A"));
    p.chartDownload = QColor(QStringLiteral("#5CA8FF"));
    p.chartUpload = QColor(QStringLiteral("#E8B36A"));
    p.heroStart = QColor(QStringLiteral("#1B3A52"));
    p.heroMid = QColor(QStringLiteral("#245C7A"));
    p.heroEnd = QColor(QStringLiteral("#2E86A8"));
    p.backdrop = QColor(0, 0, 0, 160);
    return p;
}

Palette makeLight()
{
    Palette p;
    p.dark = false;
    p.background = QColor(QStringLiteral("#F5F2FA"));
    p.surface = QColor(QStringLiteral("#FFFFFF"));
    p.surfaceContainer = QColor(QStringLiteral("#F0EBF7"));
    p.surfaceContainerHigh = QColor(QStringLiteral("#E9E2F2"));
    p.surfaceContainerHighest = QColor(QStringLiteral("#DED5EB"));
    p.onSurface = QColor(QStringLiteral("#1D1A23"));
    p.onSurfaceVariant = QColor(QStringLiteral("#625A71"));
    p.outline = QColor(QStringLiteral("#8B8199"));
    p.outlineVariant = QColor(QStringLiteral("#D8D0E4"));
    p.primary = QColor(QStringLiteral("#6B4CC8"));
    p.onPrimary = QColor(QStringLiteral("#FFFFFF"));
    p.primaryContainer = QColor(QStringLiteral("#E9DDFF"));
    p.onPrimaryContainer = QColor(QStringLiteral("#260A61"));
    p.secondary = QColor(QStringLiteral("#2E6F8F"));
    p.onSecondary = QColor(QStringLiteral("#FFFFFF"));
    p.secondaryContainer = QColor(QStringLiteral("#D2ECF7"));
    p.onSecondaryContainer = QColor(QStringLiteral("#082C3B"));
    p.tertiary = QColor(QStringLiteral("#9A452C"));
    p.error = QColor(QStringLiteral("#BA1A1A"));
    p.onError = QColor(QStringLiteral("#FFFFFF"));
    p.success = QColor(QStringLiteral("#167B49"));
    p.warning = QColor(QStringLiteral("#92600A"));
    p.chartDownload = QColor(QStringLiteral("#3D6FC0"));
    p.chartUpload = QColor(QStringLiteral("#C07A1F"));
    p.heroStart = QColor(QStringLiteral("#5A3EE0"));
    p.heroMid = QColor(QStringLiteral("#7B5AF5"));
    p.heroEnd = QColor(QStringLiteral("#A375FF"));
    p.backdrop = QColor(23, 18, 32, 120);
    return p;
}

QString rgba(const QColor& c, int alpha = -1)
{
    const int a = alpha < 0 ? c.alpha() : alpha;
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(a);
}

QString hex(const QColor& c)
{
    return c.name(QColor::HexRgb);
}
}  // namespace

Theme& Theme::instance()
{
    static Theme theme;
    return theme;
}

Theme::Theme(QObject* parent)
    : QObject(parent)
{
    m_palette = makeDark();
    m_appIcon = QIcon(QStringLiteral(":/icons/app.png"));
}

QColor Theme::alpha(const QColor& color, int a)
{
    QColor out = color;
    out.setAlpha(a);
    return out;
}

QColor Theme::blend(const QColor& a, const QColor& b, qreal t)
{
    t = qBound(0.0, t, 1.0);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

void Theme::ensureSystemConnection()
{
    if (m_systemConnected)
        return;
    m_systemConnected = true;
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) {
                if (m_mode == QStringLiteral("system"))
                    rebuild();
            });
}

void Theme::setThemeMode(const QString& mode)
{
    const QString normalized = (mode == QStringLiteral("light") || mode == QStringLiteral("system"))
                                   ? mode
                                   : QStringLiteral("dark");
    ensureSystemConnection();
    if (m_mode == normalized && !m_palette.background.isValid())
        return;
    m_mode = normalized;
    rebuild();
}

void Theme::rebuild()
{
    bool dark = true;
    if (m_mode == QStringLiteral("light")) {
        dark = false;
    } else if (m_mode == QStringLiteral("system")) {
        dark = QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
    }
    m_palette = dark ? makeDark() : makeLight();
    emit changed();
}

QString Theme::menuStyle() const
{
    const Palette& p = m_palette;
    return QString(QStringLiteral(
                       "QMenu { background-color: %1; color: %2;"
                       " border: 1px solid %3; border-radius: 8px; padding: 6px; }"
                       "QMenu::item { padding: 8px 28px 8px 12px; border-radius: 7px; }"
                       "QMenu::item:selected { background-color: %4; }"
                       "QMenu::separator { height: 1px; background: %3;"
                       " margin: 4px 8px; }"))
        .arg(p.surfaceContainerHigh.name(), p.onSurface.name(), p.outlineVariant.name(),
             p.surfaceContainerHighest.name());
}

QString Theme::scrollBarStyle() const
{
    const Palette& p = m_palette;
    return QString(QStringLiteral(
                       "QScrollBar:vertical { background: transparent; width: 12px;"
                       " margin: 2px; border: none; }"
                       "QScrollBar::handle:vertical { background: %1; min-height: 32px;"
                       " border-radius: 4px; margin: 0; }"
                       "QScrollBar::handle:vertical:hover { background: %2; }"
                       "QScrollBar::handle:vertical:pressed { background: %3; }"
                       "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
                       " height: 0px; width: 0px; background: transparent; border: none; }"
                       "QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical {"
                       " width: 0px; height: 0px; image: none; background: transparent; }"
                       "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
                       " background: transparent; }"
                       "QScrollBar:horizontal { background: transparent; height: 12px;"
                       " margin: 2px; border: none; }"
                       "QScrollBar::handle:horizontal { background: %1; min-width: 32px;"
                       " border-radius: 4px; margin: 0; }"
                       "QScrollBar::handle:horizontal:hover { background: %2; }"
                       "QScrollBar::handle:horizontal:pressed { background: %3; }"
                       "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
                       " height: 0px; width: 0px; background: transparent; border: none; }"
                       "QScrollBar::left-arrow:horizontal, QScrollBar::right-arrow:horizontal {"
                       " width: 0px; height: 0px; image: none; background: transparent; }"
                       "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
                       " background: transparent; }"
                       "QScrollBar::corner { background: transparent; border: none; }"))
        .arg(p.outline.name(), p.secondary.name(), p.primary.name());
}

QString Theme::styleSheet() const
{
    const Palette& p = m_palette;
    QString style = QString::fromUtf8(R"(
* { outline: none; }
QWidget { color: @onsurface@; }
QLabel { color: @onsurface@; background: transparent; }
QLabel[role="secondary"] { color: @secondary@; }
QLabel[role="caption"] { color: @secondary@; font-size: 12px; }
QLabel[role="micro"] { color: @secondary@; font-size: 11px; }
QLabel[role="title"] { font-size: 26px; font-weight: 700; }
QLabel[role="display"] { font-size: 32px; font-weight: 700; }
QLabel[role="section"] { font-size: 16px; font-weight: 600; }
QLabel[role="cardTitle"] { font-size: 15px; font-weight: 600; }
QLabel[role="body"] { font-size: 13px; }
QLabel[role="bodyStrong"] { font-size: 13px; font-weight: 600; }
QLabel[role="number"] { font-size: 28px; font-weight: 700; }
QLabel[role="heroTitle"] { color: #FFFFFF; font-size: 30px; font-weight: 700; }
QLabel[role="heroSub"] { color: rgba(255,255,255,175); font-size: 13px; }
QLabel[role="heroCaption"] { color: rgba(255,255,255,150); font-size: 11px; font-weight: 600; }
QLabel[role="error"] { color: @error@; }
QLabel[role="success"] { color: @success@; }
QToolTip {
    color: @onsurface@;
    background-color: @surfacehigh@;
    border: 1px solid @outlinevariant@;
    border-radius: 8px;
    padding: 6px 9px;
    font-size: 12px;
}
QLineEdit {
    color: @onsurface@;
    background-color: @surfacecontainer@;
    border: 1px solid @outlinevariant@;
    border-radius: 12px;
    padding: 0 14px;
    min-height: 42px;
    selection-background-color: @primary@;
    selection-color: @onprimary@;
}
QLineEdit:hover { border-color: @outline@; }
QLineEdit:focus { border: 1px solid @primary@; background-color: @surfacehigh@; }
QLineEdit:disabled { color: @secondary@; background-color: @outlinevariant@; }
QLineEdit[variant="embedded"] {
    background: transparent;
    border: none;
    padding: 0;
    min-height: 24px;
}
QLineEdit[variant="mono"] {
    font-family: "Cascadia Mono", "Consolas", monospace;
}
QLineEdit#editorName {
    color: @onsurface@;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 1px 4px;
    min-height: 28px;
    max-height: 34px;
    font-size: 19px;
    font-weight: 700;
}
QLineEdit#editorName:hover { background: @surfacecontainer@; }
QLineEdit#editorName:focus {
    background: @surfacecontainer@;
    border: 1px solid @outlinevariant@;
}
QLineEdit#editorField {
    min-height: 28px;
    max-height: 30px;
    border-radius: 8px;
    padding: 0 9px;
    font-size: 12px;
}
QComboBox#editorCombo {
    color: @onsurface@;
    background-color: @surfacecontainer@;
    border: 1px solid @outlinevariant@;
    border-radius: 8px;
    padding: 0 10px;
}
QComboBox#editorCombo::drop-down { border: none; width: 24px; }
QComboBox#editorCombo QAbstractItemView {
    background-color: @surfacehigh@;
    color: @onsurface@;
    border: 1px solid @outlinevariant@;
    border-radius: 8px;
    padding: 4px;
    selection-background-color: @primarycontainer@;
    selection-color: @onprimarycontainer@;
}
QPlainTextEdit {
    color: @onsurface@;
    background-color: @surfacecontainer@;
    border: 1px solid @outlinevariant@;
    border-radius: 14px;
    padding: 10px;
    selection-background-color: @primary@;
    selection-color: @onprimary@;
}
QScrollArea, QAbstractScrollArea {
    background: transparent;
    border: none;
}
QMenu {
    background-color: @surfacehigh@;
    color: @onsurface@;
    border: 1px solid @outlinevariant@;
    border-radius: 8px;
    padding: 6px;
}
QMenu::item { padding: 8px 28px 8px 12px; border-radius: 7px; }
QMenu::item:selected { background-color: @surfacehighest@; }
QMenu::separator { height: 1px; background: @outlinevariant@; margin: 4px 8px; }
QListView { background: transparent; border: none; outline: none; }
QFrame { border: none; }
)");

    const QList<QPair<QString, QString>> tokens = {
        {QStringLiteral("@onsurface@"), hex(p.onSurface)},
        {QStringLiteral("@secondary@"), hex(p.onSurfaceVariant)},
        {QStringLiteral("@error@"), hex(p.error)},
        {QStringLiteral("@success@"), hex(p.success)},
        {QStringLiteral("@surfacehigh@"), hex(p.surfaceContainerHigh)},
        {QStringLiteral("@surfacehighest@"), hex(p.surfaceContainerHighest)},
        {QStringLiteral("@surfacecontainer@"), hex(p.surfaceContainer)},
        {QStringLiteral("@outlinevariant@"), hex(p.outlineVariant)},
        {QStringLiteral("@outline@"), hex(p.outline)},
        {QStringLiteral("@primary@"), hex(p.primary)},
        {QStringLiteral("@onprimary@"), hex(p.onPrimary)},
        {QStringLiteral("@surface@"), hex(p.surface)},
    };
    for (const auto& [token, value] : tokens)
        style.replace(token, value);
    return style;
}

QIcon Theme::appIcon() const
{
    return m_appIcon;
}

QPixmap Theme::appBadge(int size) const
{
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pix(QSize(size, size) * dpr);
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient gradient(0, 0, size, size);
    gradient.setColorAt(0.0, m_palette.heroStart);
    gradient.setColorAt(0.55, m_palette.heroMid);
    gradient.setColorAt(1.0, m_palette.heroEnd);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, size, size), size * 0.3, size * 0.3);
    painter.fillPath(path, gradient);
    painter.setPen(QPen(QColor(255, 255, 255, 55), qMax(1.0, size * 0.035)));
    painter.drawEllipse(QRectF(size * 0.22, size * 0.22, size * 0.56, size * 0.56));
    painter.setFont(Fonts::icon(int(size * 0.45)));
    painter.setPen(Qt::white);
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, QStringLiteral("bolt"));
    return pix;
}
