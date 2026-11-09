#include "ui/widgets/ScriptCard.h"

#include "app/Theme.h"
#include "core/Formatters.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

using nq::scripts::ScriptMeta;
using nq::scripts::ScriptTrustState;

ScriptCard::ScriptCard(const ScriptMeta& script, QWidget* parent)
    : M3Card(parent)
    , m_script(script)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    setMinimumHeight(116);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(18, 14, 16, 14);
    root->setSpacing(14);

    auto* icon = new M3IconLabel(script.remote() ? QStringLiteral("cloud_download")
                                                 : QStringLiteral("code"),
                                 this);
    icon->setBadge(true);
    icon->setFixedBox(42, 42);
    icon->setGlyphSize(21);
    root->addWidget(icon, 0, Qt::AlignTop);

    auto* center = new QVBoxLayout;
    center->setContentsMargins(0, 1, 0, 0);
    center->setSpacing(5);

    auto* nameRow = new QHBoxLayout;
    nameRow->setContentsMargins(0, 0, 0, 0);
    nameRow->setSpacing(7);
    auto* name = new QLabel(script.name, this);
    name->setProperty("role", "cardTitle");
    name->setToolTip(script.name);
    name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    nameRow->addWidget(name, 1);

    auto* sourcePill = new M3Pill(nq::scripts::sourceKindLabel(script.kind), this);
    sourcePill->setTone(script.remote() ? M3Pill::Tone::Info : M3Pill::Tone::Neutral);
    nameRow->addWidget(sourcePill, 0, Qt::AlignVCenter);

    const ScriptTrustState trust = nq::scripts::trustState(script);
    auto* trustPill = new M3Pill(nq::scripts::trustStateLabel(trust), this);
    switch (trust) {
    case ScriptTrustState::Pinned:
        trustPill->setTone(M3Pill::Tone::Success);
        break;
    case ScriptTrustState::PinMismatch:
        trustPill->setTone(M3Pill::Tone::Error);
        trustPill->setDot(true);
        break;
    case ScriptTrustState::Untrusted:
        trustPill->setTone(M3Pill::Tone::Warning);
        trustPill->setDot(true);
        break;
    case ScriptTrustState::Trusted:
        trustPill->setTone(M3Pill::Tone::Primary);
        break;
    case ScriptTrustState::Local:
        trustPill->setTone(M3Pill::Tone::Neutral);
        break;
    }
    nameRow->addWidget(trustPill, 0, Qt::AlignVCenter);

    if (!script.enabled) {
        auto* disabledPill = new M3Pill(QStringLiteral("已停用"), this);
        disabledPill->setTone(M3Pill::Tone::Neutral);
        nameRow->addWidget(disabledPill, 0, Qt::AlignVCenter);
    }
    center->addLayout(nameRow);

    QString sourceText;
    if (script.remote()) {
        sourceText = nq::scripts::maskSecretUrl(script.url);
        if (sourceText.size() > 100)
            sourceText = sourceText.left(100) + QStringLiteral("…");
    } else {
        sourceText = script.sourcePath.isEmpty() ? QStringLiteral("在 Nyan-Q 中创建")
                                                 : QStringLiteral("本地文件 · %1")
                                                       .arg(script.sourcePath);
    }
    auto* source = new QLabel(sourceText, this);
    source->setProperty("role", "caption");
    source->setToolTip(script.url);
    source->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    center->addWidget(source);

    auto* metaRow = new QHBoxLayout;
    metaRow->setContentsMargins(0, 0, 0, 0);
    metaRow->setSpacing(12);
    auto* order = new QLabel(QStringLiteral("顺序 %1").arg(script.order), this);
    order->setProperty("role", "micro");
    metaRow->addWidget(order);
    m_association = new QLabel(this);
    m_association->setProperty("role", "micro");
    metaRow->addWidget(m_association);
    auto* hash = new QLabel(
        QStringLiteral("SHA-256 %1").arg(script.clientSha256.left(12)), this);
    hash->setProperty("role", "micro");
    hash->setToolTip(script.clientSha256);
    metaRow->addWidget(hash);
    auto* updated = new QLabel(
        QStringLiteral("更新 %1").arg(Formatters::relativeTime(script.updatedAt)), this);
    updated->setProperty("role", "micro");
    metaRow->addWidget(updated);
    if (script.remote()) {
        auto* autoText = new QLabel(
            script.autoUpdate ? QStringLiteral("自动更新 · 每 %1 小时").arg(script.updateIntervalHours)
                              : QStringLiteral("自动更新关闭"),
            this);
        autoText->setProperty("role", "micro");
        metaRow->addWidget(autoText);
    }
    if (!script.lastError.isEmpty()) {
        auto* error = new QLabel(script.lastError, this);
        error->setProperty("role", "micro");
        error->setStyleSheet(QStringLiteral("color: %1;").arg(pal().error.name()));
        error->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        metaRow->addWidget(error, 1);
    }
    metaRow->addStretch();
    center->addLayout(metaRow);

    root->addLayout(center, 1);

    auto* actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(3);

    auto* enabled = new M3Switch(this);
    enabled->setChecked(script.enabled);
    enabled->setToolTip(QStringLiteral("启用或停用脚本"));
    connect(enabled, &QAbstractButton::toggled, this,
            [this](bool checked) { emit enabledChanged(m_script.id, checked); });
    actions->addWidget(enabled, 0, Qt::AlignVCenter);

    auto* up = new M3IconButton(QStringLiteral("keyboard_arrow_up"), this);
    up->setDiameter(30);
    up->setGlyphSize(17);
    up->setToolTip(QStringLiteral("提前执行顺序"));
    connect(up, &QAbstractButton::clicked, this,
            [this] { emit moveRequested(m_script.id, -1); });
    actions->addWidget(up);

    auto* down = new M3IconButton(QStringLiteral("keyboard_arrow_down"), this);
    down->setDiameter(30);
    down->setGlyphSize(17);
    down->setToolTip(QStringLiteral("延后执行顺序"));
    connect(down, &QAbstractButton::clicked, this,
            [this] { emit moveRequested(m_script.id, 1); });
    actions->addWidget(down);

    auto* test = new M3IconButton(QStringLiteral("play_arrow"), this);
    test->setDiameter(30);
    test->setGlyphSize(17);
    test->setToolTip(QStringLiteral("使用订阅测试脚本"));
    connect(test, &QAbstractButton::clicked, this,
            [this] { emit testRequested(m_script.id); });
    actions->addWidget(test);

    auto* bindings = new M3IconButton(QStringLiteral("link"), this);
    bindings->setDiameter(30);
    bindings->setGlyphSize(17);
    bindings->setToolTip(QStringLiteral("关联订阅"));
    connect(bindings, &QAbstractButton::clicked, this,
            [this] { emit bindingsRequested(m_script.id); });
    actions->addWidget(bindings);

    if (script.remote()) {
        auto* update = new M3IconButton(QStringLiteral("refresh"), this);
        update->setDiameter(30);
        update->setGlyphSize(17);
        update->setToolTip(QStringLiteral("检查远程脚本更新"));
        connect(update, &QAbstractButton::clicked, this,
                [this] { emit updateRequested(m_script.id); });
        actions->addWidget(update);
    }

    auto* history = new M3IconButton(QStringLiteral("history"), this);
    history->setDiameter(30);
    history->setGlyphSize(17);
    history->setToolTip(QStringLiteral("历史版本与回滚"));
    connect(history, &QAbstractButton::clicked, this,
            [this] { emit historyRequested(m_script.id); });
    actions->addWidget(history);

    auto* logs = new M3IconButton(QStringLiteral("terminal"), this);
    logs->setDiameter(30);
    logs->setGlyphSize(17);
    logs->setToolTip(QStringLiteral("运行日志"));
    connect(logs, &QAbstractButton::clicked, this,
            [this] { emit logsRequested(m_script.id); });
    actions->addWidget(logs);

    auto* edit = new M3IconButton(QStringLiteral("edit"), this);
    edit->setDiameter(30);
    edit->setGlyphSize(17);
    edit->setToolTip(QStringLiteral("编辑脚本"));
    connect(edit, &QAbstractButton::clicked, this,
            [this] { emit editRequested(m_script.id); });
    actions->addWidget(edit);

    auto* remove = new M3IconButton(QStringLiteral("delete_outline"), this);
    remove->setDiameter(30);
    remove->setGlyphSize(17);
    remove->setDangerOnHover(true);
    remove->setToolTip(QStringLiteral("删除脚本"));
    connect(remove, &QAbstractButton::clicked, this,
            [this] { emit deleteRequested(m_script.id); });
    actions->addWidget(remove);

    root->addLayout(actions, 0);
}

void ScriptCard::setAssociationText(const QString& text, const QString& tooltip)
{
    if (!m_association)
        return;
    m_association->setText(text);
    m_association->setToolTip(tooltip.isEmpty() ? text : tooltip);
    m_association->setVisible(!text.isEmpty());
}

void ScriptCard::paintEvent(QPaintEvent* event)
{
    M3Card::paintEvent(event);
    if (!m_script.enabled)
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(pal().primary);
    painter.drawRoundedRect(QRectF(2.5, 24, 3, height() - 48), 1.5, 1.5);
}
