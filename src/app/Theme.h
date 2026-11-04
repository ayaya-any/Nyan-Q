#pragma once

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QObject>
#include <QString>

struct Palette {
    bool dark = true;
    QColor background;
    QColor surface;
    QColor surfaceContainer;
    QColor surfaceContainerHigh;
    QColor surfaceContainerHighest;
    QColor onSurface;
    QColor onSurfaceVariant;
    QColor outline;
    QColor outlineVariant;
    QColor primary;
    QColor onPrimary;
    QColor primaryContainer;
    QColor onPrimaryContainer;
    QColor secondary;
    QColor onSecondary;
    QColor secondaryContainer;
    QColor onSecondaryContainer;
    QColor tertiary;
    QColor error;
    QColor onError;
    QColor success;
    QColor warning;
    QColor chartDownload;
    QColor chartUpload;
    QColor heroStart;
    QColor heroMid;
    QColor heroEnd;
    QColor backdrop;
};

class Theme final : public QObject {
    Q_OBJECT

public:
    static Theme& instance();

    void setThemeMode(const QString& mode);
    QString themeMode() const { return m_mode; }
    bool isDark() const { return m_palette.dark; }
    const Palette& palette() const { return m_palette; }
    QString styleSheet() const;
    QString scrollBarStyle() const;
    QString menuStyle() const;
    QIcon appIcon() const;
    QIcon trayIcon(const QString& name) const;
    QPixmap appBadge(int size) const;

    static QColor alpha(const QColor& color, int a);
    static QColor blend(const QColor& a, const QColor& b, qreal t);

signals:
    void changed();

private:
    explicit Theme(QObject* parent = nullptr);
    void rebuild();
    void ensureSystemConnection();

    QString m_mode = QStringLiteral("dark");
    Palette m_palette;
    QIcon m_appIcon;
    mutable QHash<QString, QIcon> m_trayIcons;
    bool m_systemConnected = false;
};

inline const Palette& pal()
{
    return Theme::instance().palette();
}
