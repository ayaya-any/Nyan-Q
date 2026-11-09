#include "ui/pages/ProfilesPage.h"

#include "app/Fonts.h"
#include "app/Theme.h"
#include "core/ClashController.h"
#include "ui/PageHeader.h"
#include "ui/dialogs/ConfirmDialog.h"
#include "ui/dialogs/ProfileDialog.h"
#include "ui/widgets/MaterialWidgets.h"
#include "ui/widgets/ProfileCard.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include <algorithm>

ProfilesPage::ProfilesPage(ClashController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();
    wire();
    refresh();
}

void ProfilesPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(32, 18, 32, 22);
    root->setSpacing(14);

    m_header = new PageHeader(QStringLiteral("订阅与配置"), {}, this);

    m_busyPill = new M3Pill(QStringLiteral("正在更新"), this);
    m_busyPill->setTone(M3Pill::Tone::Warning);
    m_busyPill->setDot(true);
    m_busyPill->hide();
    m_header->addAction(m_busyPill);

    m_importFileButton =
        new M3Button(QStringLiteral("导入文件"), M3Button::Variant::Outlined, this);
    m_importFileButton->setIconText(QStringLiteral("upload_file"));
    m_header->addAction(m_importFileButton);

    m_importUrlButton = new M3Button(QStringLiteral("导入订阅"), M3Button::Variant::Filled, this);
    m_importUrlButton->setIconText(QStringLiteral("add_link"));
    m_header->addAction(m_importUrlButton);
    root->addWidget(m_header);

    auto* summaryRow = new QHBoxLayout;
    summaryRow->setContentsMargins(2, 0, 2, 0);
    summaryRow->setSpacing(8);
    m_countLabel = new QLabel(this);
    m_countLabel->setProperty("role", "caption");
    summaryRow->addWidget(m_countLabel);
    summaryRow->addStretch();
    auto* hint = new QLabel(QStringLiteral("订阅仅保存在本机，不会上传"), this);
    hint->setProperty("role", "micro");
    summaryRow->addWidget(hint);
    root->addLayout(summaryRow);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    root->addWidget(m_scroll, 1);

    m_content = new QWidget(m_scroll);
    m_content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_scroll->setWidget(m_content);

    m_listLayout = new QVBoxLayout(m_content);
    m_listLayout->setContentsMargins(0, 2, 4, 6);
    m_listLayout->setSpacing(12);
    m_listLayout->addStretch();

    connect(m_importUrlButton, &QAbstractButton::clicked, this, &ProfilesPage::importUrl);
    connect(m_importFileButton, &QAbstractButton::clicked, this, &ProfilesPage::importFile);
}

void ProfilesPage::wire()
{
    ProfileStore* store = m_controller->profileStore();
    connect(store, &ProfileStore::changed, this, &ProfilesPage::rebuild);
    connect(store, &ProfileStore::busyChanged, this,
            [this](bool) { updateBusyState(); });
    connect(m_controller, &ClashController::activeProfileChanged, this,
            [this] { rebuild(); });
}

void ProfilesPage::refresh()
{
    updateBusyState();
    rebuild();
}

void ProfilesPage::updateBusyState()
{
    const bool busy = m_controller->profileStore()->isBusy();
    m_busyPill->setVisible(busy);
    m_importUrlButton->setEnabled(!busy);
    m_importFileButton->setEnabled(!busy);
}

void ProfilesPage::importUrl()
{
    ProfileDialog dialog(window());
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_controller->importUrl(dialog.profileName(), dialog.profileUrl());
}

void ProfilesPage::importFile()
{
    const QString path = QFileDialog::getOpenFileName(
        window(), QStringLiteral("选择 Clash 配置文件"), QString(),
        QStringLiteral("Clash 配置 (*.yaml *.yml);;所有文件 (*.*)"));
    if (!path.isEmpty())
        m_controller->importFile(path);
}

void ProfilesPage::openActiveConfigEditor()
{
    const auto active = m_controller->profileStore()->activeProfile();
    if (active)
        editProfile(*active);
}

void ProfilesPage::editProfile(const cq::ProfileMeta& profile)
{
    emit editConfigRequested(profile.id);
}

void ProfilesPage::requestDelete(const cq::ProfileMeta& profile)
{
    const QString message =
        profile.active
            ? QStringLiteral("「%1」当前正在使用。删除后将自动切换到其他配置，"
                             "并重启正在运行的 Mihomo 内核。")
                  .arg(profile.name)
            : QStringLiteral("将删除本地保存的「%1」，此操作无法撤销。").arg(profile.name);
    if (ConfirmDialog::ask(window(), QStringLiteral("删除配置"), message, QStringLiteral("删除"),
                           true)) {
        m_controller->removeProfile(profile.id);
    }
}

void ProfilesPage::rebuild()
{
    if (!m_listLayout)
        return;

    const int scrollValue = m_scroll->verticalScrollBar()->value();
    while (m_listLayout->count() > 0) {
        QLayoutItem* item = m_listLayout->takeAt(0);
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    QVector<cq::ProfileMeta> profiles = m_controller->profileStore()->profiles();
    std::stable_sort(profiles.begin(), profiles.end(),
                     [](const cq::ProfileMeta& a, const cq::ProfileMeta& b) {
                         if (a.active != b.active)
                             return a.active;
                         return a.updatedAt > b.updatedAt;
                     });

    int subscriptions = 0;
    for (const cq::ProfileMeta& profile : profiles) {
        if (profile.remote())
            ++subscriptions;
        auto* card = new ProfileCard(profile, m_content);
        connect(card, &ProfileCard::activateRequested, this, [this](const QString& id) {
            m_controller->activateProfile(id);
        });
        connect(card, &ProfileCard::updateRequested, this, [this](const QString& id) {
            m_controller->refreshProfile(id);
        });
        connect(card, &ProfileCard::editRequested, this, [this, profile](const QString&) {
            editProfile(profile);
        });
        connect(card, &ProfileCard::createScriptRequested, this,
                [this](const QString& id) { emit createScriptRequested(id); });
        connect(card, &ProfileCard::deleteRequested, this, [this, profile] {
            requestDelete(profile);
        });
        m_listLayout->addWidget(card);
    }

    if (profiles.isEmpty()) {
        auto* empty = new M3EmptyState(
            QStringLiteral("layers"), QStringLiteral("还没有配置文件"),
            QStringLiteral("导入 Clash 订阅或本地 YAML 配置以开始使用。"), m_content);
        m_listLayout->addWidget(empty);
    }

    m_listLayout->addStretch(1);
    m_countLabel->setText(QStringLiteral("%1 个配置 · %2 个远程订阅")
                              .arg(profiles.size())
                              .arg(subscriptions));
    m_scroll->verticalScrollBar()->setValue(scrollValue);
}
