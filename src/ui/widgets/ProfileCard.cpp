#include "ui/widgets/ProfileCard.h"

#include "app/Fonts.h"
#include "app/Theme.h"
#include "core/Formatters.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QUrl>
#include <QVBoxLayout>

ProfileCard::ProfileCard(const cq::ProfileMeta& profile, QWidget* parent)
    : M3Card(parent)
    , m_profile(profile)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    setMinimumHeight(118);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(20, 16, 18, 16);
    root->setSpacing(16);

    auto* icon = new M3IconLabel(profile.type == QStringLiteral("remote")
                                     ? QStringLiteral("cloud_download")
                                     : profile.type == QStringLiteral("builtin")
                                           ? QStringLiteral("bolt")
                                           : QStringLiteral("description"),
                                 this);
    icon->setBadge(true);
    icon->setFixedBox(44, 44);
    icon->setGlyphSize(22);
    root->addWidget(icon, 0, Qt::AlignTop);

    auto* center = new QVBoxLayout;
    center->setContentsMargins(0, 1, 0, 0);
    center->setSpacing(5);

    auto* nameRow = new QHBoxLayout;
    nameRow->setContentsMargins(0, 0, 0, 0);
    nameRow->setSpacing(8);
    auto* name = new QLabel(profile.name, this);
    name->setProperty("role", "cardTitle");
    name->setToolTip(profile.name);
    name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    nameRow->addWidget(name, 1);

    auto* typePill = new M3Pill(cq::profileTypeLabel(profile.type), this);
    typePill->setTone(profile.remote() ? M3Pill::Tone::Info : M3Pill::Tone::Neutral);
    nameRow->addWidget(typePill, 0, Qt::AlignVCenter);

    if (profile.active) {
        auto* activePill = new M3Pill(QStringLiteral("使用中"), this);
        activePill->setTone(M3Pill::Tone::Primary);
        activePill->setDot(true);
        nameRow->addWidget(activePill, 0, Qt::AlignVCenter);
    }
    center->addLayout(nameRow);

    QString sourceText;
    if (profile.remote()) {
        const QUrl url(profile.url);
        sourceText = url.host().isEmpty() ? profile.url : url.host();
        if (sourceText.size() > 64)
            sourceText = sourceText.left(64) + QStringLiteral("…");
        sourceText = QStringLiteral("订阅 · %1").arg(sourceText);
    } else if (profile.type == QStringLiteral("builtin")) {
        sourceText = QStringLiteral("随应用内置的直连配置");
    } else {
        sourceText = QStringLiteral("本地配置文件");
    }
    auto* source = new QLabel(sourceText, this);
    source->setProperty("role", "caption");
    source->setToolTip(profile.url.isEmpty() ? sourceText : profile.url);
    center->addWidget(source);

    auto* metaRow = new QHBoxLayout;
    metaRow->setContentsMargins(0, 0, 0, 0);
    metaRow->setSpacing(12);
    auto* updated = new QLabel(
        QStringLiteral("更新于 %1").arg(Formatters::relativeTime(profile.updatedAt)), this);
    updated->setProperty("role", "caption");
    metaRow->addWidget(updated);
    if (profile.remote()) {
        auto* expiry = new QLabel(
            QStringLiteral("到期 %1").arg(Formatters::expiry(profile.expireAt)), this);
        expiry->setProperty("role", "caption");
        metaRow->addWidget(expiry);
    }
    metaRow->addStretch();
    center->addLayout(metaRow);

    if (profile.total > 0) {
        const qint64 used = profile.upload + profile.download;
        auto* usageHeader = new QHBoxLayout;
        usageHeader->setContentsMargins(0, 2, 0, 0);
        auto* usedLabel = new QLabel(
            QStringLiteral("已用 %1 / %2").arg(Formatters::bytes(used), Formatters::bytes(profile.total)),
            this);
        usedLabel->setProperty("role", "micro");
        auto* percentLabel = new QLabel(Formatters::percent(used, profile.total), this);
        percentLabel->setProperty("role", "micro");
        usageHeader->addWidget(usedLabel);
        usageHeader->addStretch();
        usageHeader->addWidget(percentLabel);
        center->addLayout(usageHeader);

        auto* bar = new M3ProgressBar(this);
        bar->setBarHeight(5);
        bar->setValue(100.0 * static_cast<double>(used) / static_cast<double>(profile.total));
        center->addWidget(bar);
    }

    root->addLayout(center, 1);

    auto* actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(5);
    if (!profile.active) {
        auto* useButton = new M3Button(QStringLiteral("启用"), M3Button::Variant::Tonal, this);
        useButton->setIconText(QStringLiteral("check"));
        useButton->setCompact(true);
        connect(useButton, &QAbstractButton::clicked, this,
                [this] { emit activateRequested(m_profile.id); });
        actions->addWidget(useButton, 0, Qt::AlignVCenter);
    }

    if (profile.remote()) {
        auto* update = new M3IconButton(QStringLiteral("refresh"), this);
        update->setDiameter(32);
        update->setGlyphSize(17);
        update->setToolTip(QStringLiteral("立即更新订阅"));
        connect(update, &QAbstractButton::clicked, this,
                [this] { emit updateRequested(m_profile.id); });
        actions->addWidget(update);
    }

    auto* edit = new M3IconButton(QStringLiteral("edit"), this);
    edit->setDiameter(32);
    edit->setGlyphSize(17);
    edit->setToolTip(QStringLiteral("编辑 YAML 配置"));
    edit->setAccessibleName(QStringLiteral("编辑配置 %1").arg(profile.name));
    connect(edit, &QAbstractButton::clicked, this,
            [this] { emit editRequested(m_profile.id); });
    actions->addWidget(edit);

    auto* remove = new M3IconButton(QStringLiteral("delete_outline"), this);
    remove->setDiameter(32);
    remove->setGlyphSize(17);
    remove->setDangerOnHover(true);
    remove->setToolTip(QStringLiteral("删除配置"));
    connect(remove, &QAbstractButton::clicked, this,
            [this] { emit deleteRequested(m_profile.id); });
    actions->addWidget(remove);

    root->addLayout(actions, 0);
}

void ProfileCard::paintEvent(QPaintEvent* event)
{
    M3Card::paintEvent(event);
    if (!m_profile.active)
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(pal().primary);
    painter.drawRoundedRect(QRectF(2.5, 24, 3, height() - 48), 1.5, 1.5);
}
