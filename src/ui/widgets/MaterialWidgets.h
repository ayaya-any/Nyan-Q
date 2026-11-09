#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QFont>
#include <QPropertyAnimation>
#include <QStringList>
#include <QTimer>
#include <QWidget>

class QGraphicsOpacityEffect;
class QLabel;
class QParallelAnimationGroup;
class QVariantAnimation;

class M3Button : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)

public:
    enum class Variant { Filled, Tonal, Outlined, Text, Elevated, Danger };

    explicit M3Button(QWidget* parent = nullptr);
    M3Button(const QString& text, Variant variant = Variant::Filled, QWidget* parent = nullptr);

    void setVariant(Variant variant);
    Variant variant() const { return m_variant; }
    void setIconText(const QString& glyph);
    QString iconText() const { return m_icon; }
    void setCompact(bool compact);
    bool compact() const { return m_compact; }
    void setAccent(const QColor& background, const QColor& foreground);
    void clearAccent();
    void setPill(bool pill);
    void setLeadingIconOnly(bool leadingOnly);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    QColor backgroundForState() const;
    QColor foregroundForState() const;
    QColor borderForState() const;
    QFont buttonFont() const;

    Variant m_variant = Variant::Filled;
    QString m_icon;
    bool m_compact = false;
    bool m_pill = false;
    bool m_leadingIconOnly = false;
    bool m_hasAccent = false;
    QColor m_accentBackground;
    QColor m_accentForeground;
    qreal m_hoverProgress = 0.0;
    QVariantAnimation* m_hoverAnimation = nullptr;
};

class M3IconButton : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)

public:
    enum class Kind { Standard, Tonal, Filled, Text };

    explicit M3IconButton(const QString& glyph, QWidget* parent = nullptr);

    void setGlyph(const QString& glyph);
    void setGlyphSize(int pixelSize);
    void setDiameter(int diameter);
    void setKind(Kind kind);
    void setDangerOnHover(bool danger);
    void setIconColor(const QColor& color);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QString m_glyph;
    int m_glyphSize = 20;
    int m_diameter = 38;
    Kind m_kind = Kind::Standard;
    bool m_dangerOnHover = false;
    bool m_hasIconColor = false;
    QColor m_iconColor;
    qreal m_hoverProgress = 0.0;
    QVariantAnimation* m_hoverAnimation = nullptr;
};

class M3Switch : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(qreal progress READ progress WRITE setProgress)

public:
    explicit M3Switch(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    qreal progress() const { return m_progress; }
    void setProgress(qreal value);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    qreal m_progress = 0.0;
    QPropertyAnimation* m_animation = nullptr;
};

class M3Card : public QWidget {
    Q_OBJECT

public:
    explicit M3Card(QWidget* parent = nullptr, int radius = 20);

    void setRadius(int radius);
    int radius() const { return m_radius; }
    void setSurfaceColor(const QColor& color);
    void clearSurfaceColor();
    void setBorderColor(const QColor& color);
    void clearBorderColor();
    void setDrawBorder(bool draw);
    void setPadding(int padding);
    int padding() const { return m_padding; }
    void setLayoutMargins(int horizontal, int vertical);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int m_radius = 20;
    int m_padding = 0;
    bool m_hasSurface = false;
    bool m_drawBorder = true;
    QColor m_surfaceColor;
    QColor m_borderColor;
};

class M3IconLabel : public QWidget {
    Q_OBJECT

public:
    explicit M3IconLabel(const QString& glyph, QWidget* parent = nullptr);

    void setGlyph(const QString& glyph);
    void setGlyphSize(int pixelSize);
    void setColor(const QColor& color);
    void clearColor();
    void setBadge(bool badge, const QColor& background = {});
    void setFixedBox(int width, int height);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_glyph;
    int m_glyphSize = 20;
    bool m_hasColor = false;
    QColor m_color;
    bool m_badge = false;
    QColor m_badgeColor;
    QSize m_box = QSize(24, 24);
};

class M3SegmentedControl : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal indicator READ indicator WRITE setIndicator)

public:
    explicit M3SegmentedControl(const QStringList& options, QWidget* parent = nullptr);

    void setOptions(const QStringList& options);
    QStringList options() const { return m_options; }
    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);
    void setCompact(bool compact);
    QSize sizeHint() const override;
    qreal indicator() const { return m_indicator; }
    void setIndicator(qreal value);

signals:
    void currentChanged(int index, const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void choose(int index, bool emitSignal);

    QStringList m_options;
    int m_currentIndex = 0;
    int m_hoverIndex = -1;
    bool m_compact = false;
    qreal m_indicator = 0.0;
    QPropertyAnimation* m_animation = nullptr;
};

class M3Pill : public QWidget {
    Q_OBJECT

public:
    enum class Tone { Neutral, Primary, Success, Warning, Error, Info };

    explicit M3Pill(const QString& text = {}, QWidget* parent = nullptr);

    void setText(const QString& text);
    QString text() const { return m_text; }
    void setTone(Tone tone);
    void setDot(bool dot);
    void setDotColor(const QColor& color);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_text;
    Tone m_tone = Tone::Neutral;
    bool m_dot = false;
    bool m_hasDotColor = false;
    QColor m_dotColor;
};

class M3FilterChip : public QAbstractButton {
    Q_OBJECT

public:
    explicit M3FilterChip(const QString& text, QWidget* parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    bool m_hover = false;
};

class M3ProgressBar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal animatedValue READ animatedValue WRITE setAnimatedValue)

public:
    explicit M3ProgressBar(QWidget* parent = nullptr);

    void setValue(qreal value);
    void setBarHeight(int height);
    qreal value() const { return m_value; }
    qreal animatedValue() const { return m_animatedValue; }
    void setAnimatedValue(qreal value);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    qreal m_value = 0.0;
    qreal m_animatedValue = 0.0;
    int m_barHeight = 6;
    QPropertyAnimation* m_animation = nullptr;
};

class M3Divider : public QWidget {
    Q_OBJECT

public:
    explicit M3Divider(QWidget* parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
};

class M3EmptyState : public QWidget {
    Q_OBJECT

public:
    explicit M3EmptyState(const QString& glyph, const QString& title,
                          const QString& subtitle = {}, QWidget* parent = nullptr);
    QAbstractButton* addAction(const QString& text, M3Button::Variant variant = M3Button::Variant::Tonal);
    void setTitle(const QString& title);
    void setSubtitle(const QString& subtitle);

private:
    void updateMinimumHeight();

    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    M3IconLabel* m_icon = nullptr;
    QWidget* m_actionHost = nullptr;
};

class M3SettingRow : public QWidget {
    Q_OBJECT

public:
    explicit M3SettingRow(const QString& title, const QString& subtitle = {},
                          QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setSubtitle(const QString& subtitle);
    void setLeadingIcon(const QString& glyph);
    void setTrailing(QWidget* widget);
    QWidget* trailing() const { return m_trailing; }
    void setContentMargins(int left, int top, int right, int bottom);

private:
    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    M3IconLabel* m_icon = nullptr;
    QWidget* m_trailing = nullptr;
    QWidget* m_trailingHost = nullptr;
};

class M3Snackbar : public QWidget {
    Q_OBJECT

public:
    explicit M3Snackbar(QWidget* parent = nullptr);

    void showMessage(const QString& message, bool error = false);
    void reposition();
    void dismissNow();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void hideAnimated();

    QLabel* m_label = nullptr;
    M3IconLabel* m_icon = nullptr;
    QTimer m_hideTimer;
    QPropertyAnimation* m_positionAnimation = nullptr;
    QPropertyAnimation* m_opacityAnimation = nullptr;
    QGraphicsOpacityEffect* m_opacity = nullptr;
    bool m_error = false;
};
