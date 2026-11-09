#include "ui/pages/ScriptsPage.h"

#include "app/Theme.h"
#include "core/ClashController.h"
#include "scripts/ScriptManager.h"
#include "ui/PageHeader.h"
#include "ui/dialogs/ConfirmDialog.h"
#include "ui/dialogs/ScriptDialogs.h"
#include "ui/editor/ScriptEditorView.h"
#include "ui/widgets/MaterialWidgets.h"
#include "ui/widgets/ScriptCard.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

using namespace nq::scripts;

ScriptsPage::ScriptsPage(ClashController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();
    wire();
    refresh();
}

void ScriptsPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack);

    m_listPage = new QWidget(m_stack);
    auto* listRoot = new QVBoxLayout(m_listPage);
    listRoot->setContentsMargins(32, 18, 32, 22);
    listRoot->setSpacing(14);

    m_header = new PageHeader(QStringLiteral("脚本"), {}, m_listPage);
    m_busyPill = new M3Pill(QStringLiteral("正在检查更新"), m_listPage);
    m_busyPill->setTone(M3Pill::Tone::Warning);
    m_busyPill->setDot(true);
    m_busyPill->hide();
    m_header->addAction(m_busyPill);

    auto* settingsButton = new M3IconButton(QStringLiteral("tune"), m_listPage);
    settingsButton->setDiameter(36);
    settingsButton->setGlyphSize(18);
    settingsButton->setToolTip(QStringLiteral("脚本执行设置"));
    m_header->addAction(settingsButton);

    auto* previewButton =
        new M3Button(QStringLiteral("预览最终配置"), M3Button::Variant::Outlined, m_listPage);
    previewButton->setIconText(QStringLiteral("preview"));
    m_header->addAction(previewButton);

    m_importFileButton =
        new M3Button(QStringLiteral("导入文件"), M3Button::Variant::Outlined, m_listPage);
    m_importFileButton->setIconText(QStringLiteral("upload_file"));
    m_header->addAction(m_importFileButton);

    m_importUrlButton =
        new M3Button(QStringLiteral("导入 URL"), M3Button::Variant::Outlined, m_listPage);
    m_importUrlButton->setIconText(QStringLiteral("add_link"));
    m_header->addAction(m_importUrlButton);

    m_newButton = new M3Button(QStringLiteral("新建脚本"), M3Button::Variant::Filled, m_listPage);
    m_newButton->setIconText(QStringLiteral("add"));
    m_header->addAction(m_newButton);
    listRoot->addWidget(m_header);

    auto* summaryRow = new QHBoxLayout;
    summaryRow->setContentsMargins(2, 0, 2, 0);
    summaryRow->setSpacing(10);
    m_countLabel = new QLabel(m_listPage);
    m_countLabel->setProperty("role", "caption");
    summaryRow->addWidget(m_countLabel);
    m_feedback = new QLabel(m_listPage);
    m_feedback->setProperty("role", "micro");
    m_feedback->setWordWrap(true);
    summaryRow->addWidget(m_feedback, 1);
    auto* hint = new QLabel(QStringLiteral("全局脚本先执行，随后执行订阅专属脚本"), m_listPage);
    hint->setProperty("role", "micro");
    summaryRow->addWidget(hint);
    listRoot->addLayout(summaryRow);

    m_scroll = new QScrollArea(m_listPage);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listRoot->addWidget(m_scroll, 1);

    m_content = new QWidget(m_scroll);
    m_content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_scroll->setWidget(m_content);
    m_listLayout = new QVBoxLayout(m_content);
    m_listLayout->setContentsMargins(0, 2, 4, 6);
    m_listLayout->setSpacing(12);
    m_listLayout->addStretch();
    m_stack->addWidget(m_listPage);

    m_editorPage = new QWidget(m_stack);
    m_editorLayout = new QVBoxLayout(m_editorPage);
    m_editorLayout->setContentsMargins(0, 0, 0, 0);
    m_editorLayout->setSpacing(0);
    m_stack->addWidget(m_editorPage);
    m_stack->setCurrentWidget(m_listPage);

    connect(m_newButton, &QAbstractButton::clicked, this, &ScriptsPage::createScript);
    connect(m_importFileButton, &QAbstractButton::clicked, this, &ScriptsPage::importFile);
    connect(m_importUrlButton, &QAbstractButton::clicked, this, &ScriptsPage::importUrl);
    connect(settingsButton, &QAbstractButton::clicked, this, &ScriptsPage::openSettings);
    connect(previewButton, &QAbstractButton::clicked, this, &ScriptsPage::previewAll);
}

void ScriptsPage::wire()
{
    ScriptManager* manager = m_controller->scriptManager();
    connect(manager, &ScriptManager::changed, this, &ScriptsPage::rebuild);
    connect(manager, &ScriptManager::busyChanged, this, [this](bool) { updateBusyState(); });
    connect(manager, &ScriptManager::operationSucceeded, this,
            [this](const QString& message) { setFeedback(message, false); });
    connect(manager, &ScriptManager::operationFailed, this,
            [this](const QString& message) { setFeedback(message, true); });
    connect(manager, &ScriptManager::updateFinished, this,
            [this](const ScriptUpdateResult& result) {
                updateBusyState();
                if (!result.ok) {
                    setFeedback(result.error, true);
                    return;
                }
                if (result.ok && !result.notModified && !result.skipped) {
                    setFeedback(QStringLiteral("脚本「%1」已更新").arg(result.scriptName), false);
                    if (result.manual && !m_editor && !result.oldContent.isEmpty()) {
                        ScriptUpdateDiffDialog dialog(result, window());
                        dialog.exec();
                    }
                }
                rebuild();
            });
}

void ScriptsPage::refresh()
{
    updateBusyState();
    rebuild();
}

void ScriptsPage::updateBusyState()
{
    const bool busy = m_controller->scriptManager()->isBusy();
    m_busyPill->setVisible(busy);
    m_newButton->setEnabled(!busy);
    m_importFileButton->setEnabled(!busy);
    m_importUrlButton->setEnabled(!busy);
}

void ScriptsPage::rebuild()
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

    QVector<ScriptMeta> scripts = m_controller->scriptManager()->scripts();
    std::stable_sort(scripts.begin(), scripts.end(),
                     [](const ScriptMeta& a, const ScriptMeta& b) {
                         if (a.order != b.order)
                             return a.order < b.order;
                         return QString::localeAwareCompare(a.name, b.name) < 0;
                     });

    const QVector<cq::ProfileMeta> profiles = m_controller->profileStore()->profiles();
    const ScriptBindingManager& bindings = m_controller->scriptManager()->bindings();
    const auto associationFor = [&](const ScriptMeta& script) {
        if (bindings.isGlobalEnabled(script.id)) {
            QStringList excluded;
            for (const cq::ProfileMeta& profile : profiles) {
                if (bindings.isProfileExcluded(script.id, profile.id))
                    excluded.append(profile.name);
            }
            return qMakePair(QStringLiteral("关联：全局"),
                             excluded.isEmpty()
                                 ? QStringLiteral("全局脚本，适用于所有配置")
                                 : QStringLiteral("全局脚本；排除：%1")
                                       .arg(excluded.join(QStringLiteral("、"))));
        }

        QStringList names;
        for (const cq::ProfileMeta& profile : profiles) {
            if (bindings.isEffective(script.id, profile.id))
                names.append(profile.name);
        }
        if (names.isEmpty()) {
            return qMakePair(QStringLiteral("关联：未关联"),
                             QStringLiteral("当前脚本不会在任何配置上执行"));
        }
        QString label;
        if (names.size() > 2) {
            label = QStringLiteral("关联：%1、%2 等 %3 个")
                        .arg(names.at(0), names.at(1))
                        .arg(names.size());
        } else {
            label = QStringLiteral("关联：%1").arg(names.join(QStringLiteral("、")));
        }
        return qMakePair(label, names.join(QStringLiteral("、")));
    };

    int enabled = 0;
    int remote = 0;
    for (const ScriptMeta& script : scripts) {
        if (script.enabled)
            ++enabled;
        if (script.remote())
            ++remote;
        auto* card = new ScriptCard(script, m_content);
        const auto association = associationFor(script);
        card->setAssociationText(association.first, association.second);
        connect(card, &ScriptCard::editRequested, this, &ScriptsPage::openEditor);
        connect(card, &ScriptCard::deleteRequested, this, &ScriptsPage::deleteScript);
        connect(card, &ScriptCard::bindingsRequested, this, &ScriptsPage::openBindings);
        connect(card, &ScriptCard::historyRequested, this, &ScriptsPage::openHistory);
        connect(card, &ScriptCard::logsRequested, this, &ScriptsPage::openLogs);
        connect(card, &ScriptCard::updateRequested, this, &ScriptsPage::checkUpdate);
        connect(card, &ScriptCard::testRequested, this, &ScriptsPage::testScript);
        connect(card, &ScriptCard::enabledChanged, this,
                [this](const QString& id, bool value) {
                    QString error;
                    if (!m_controller->scriptManager()->setEnabled(id, value, &error))
                        setFeedback(error, true);
                    rebuild();
                });
        connect(card, &ScriptCard::moveRequested, this,
                [this](const QString& id, int delta) {
                    QString error;
                    if (!m_controller->scriptManager()->moveScript(id, delta, &error))
                        setFeedback(error, true);
                    rebuild();
                });
        m_listLayout->addWidget(card);
    }

    if (scripts.isEmpty()) {
        auto* empty = new M3EmptyState(
            QStringLiteral("code"), QStringLiteral("还没有 JavaScript 脚本"),
            QStringLiteral("新建一个脚本，或从本地文件 / HTTPS URL 导入。"
                           "脚本不会在 UI 线程执行。"),
            m_content);
        auto* action = empty->addAction(QStringLiteral("新建脚本"));
        connect(action, &QAbstractButton::clicked, this, &ScriptsPage::createScript);
        m_listLayout->addWidget(empty);
    }

    m_listLayout->addStretch(1);
    m_countLabel->setText(QStringLiteral("%1 个脚本 · %2 个已启用 · %3 个远程")
                              .arg(scripts.size())
                              .arg(enabled)
                              .arg(remote));
    m_scroll->verticalScrollBar()->setValue(scrollValue);
}

void ScriptsPage::clearEditor()
{
    if (m_editor) {
        m_editorLayout->removeWidget(m_editor);
        m_editor->deleteLater();
        m_editor = nullptr;
    }
}

void ScriptsPage::openEditor(const QString& id)
{
    if (m_editor && m_editor->scriptId() == id) {
        m_stack->setCurrentWidget(m_editorPage);
        return;
    }
    if (m_editor && !m_editor->confirmClose())
        return;
    clearEditor();

    m_editor = new ScriptEditorView(m_controller, id, m_editorPage);
    m_editorLayout->addWidget(m_editor);
    connect(m_editor, &ScriptEditorView::closeRequested, this, &ScriptsPage::closeEditor);
    connect(m_editor, &ScriptEditorView::saved, this, &ScriptsPage::rebuild);
    m_stack->setCurrentWidget(m_editorPage);
}

void ScriptsPage::closeEditor()
{
    clearEditor();
    m_stack->setCurrentWidget(m_listPage);
    rebuild();
    refresh();
}

void ScriptsPage::createScript()
{
    ScriptNameDialog nameDialog(window());
    if (nameDialog.exec() != QDialog::Accepted)
        return;
    QString error;
    const QString id = m_controller->scriptManager()->createLocalScript(
        nameDialog.scriptName(), QString(), QString(), &error);
    if (id.isEmpty()) {
        setFeedback(error, true);
        return;
    }
    rebuild();
    openEditor(id);
}

void ScriptsPage::createScriptForProfile(const QString& profileId)
{
    const auto profile = m_controller->profileStore()->find(profileId);
    if (!profile) {
        setFeedback(QStringLiteral("找不到这个配置"), true);
        return;
    }

    ScriptNameDialog nameDialog(window(), QStringLiteral("%1 · 脚本").arg(profile->name));
    if (nameDialog.exec() != QDialog::Accepted)
        return;

    ScriptManager* manager = m_controller->scriptManager();
    QString error;
    const QString id = manager->createLocalScript(
        nameDialog.scriptName(), QStringLiteral("由配置「%1」创建").arg(profile->name),
        QString(), &error);
    if (id.isEmpty()) {
        setFeedback(error, true);
        return;
    }

    // Scripts created from a profile card are strongly scoped to that profile:
    // they never run for other subscriptions unless the user later changes the
    // binding explicitly.
    if (!manager->setGlobalBinding(id, false, &error) ||
        !manager->setProfileBinding(id, profile->id, true, &error)) {
        setFeedback(error, true);
    } else {
        setFeedback(QStringLiteral("已为「%1」创建专属脚本").arg(profile->name), false);
    }
    rebuild();
    openEditor(id);
}

void ScriptsPage::importFile()
{
    const QString path = QFileDialog::getOpenFileName(
        window(), QStringLiteral("选择 JavaScript 文件"), QString(),
        QStringLiteral("JavaScript (*.js *.mjs);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    QString error;
    const QString id = m_controller->scriptManager()->importLocalFile(path, QString(), &error);
    if (id.isEmpty()) {
        setFeedback(error, true);
        return;
    }
    setFeedback(QStringLiteral("已导入本地脚本"), false);
    rebuild();
    openEditor(id);
}

void ScriptsPage::importUrl()
{
    ScriptUrlDialog dialog(window());
    if (dialog.exec() != QDialog::Accepted)
        return;
    QString error;
    if (!m_controller->scriptManager()->importRemote(dialog.scriptName(), dialog.scriptUrl(),
                                                     true, &error)) {
        setFeedback(error, true);
        return;
    }
    setFeedback(QStringLiteral("正在下载远程脚本…"), false);
    updateBusyState();
}

void ScriptsPage::openSettings()
{
    ScriptSettingsDialog dialog(m_controller->scriptManager(), window());
    dialog.exec();
}

void ScriptsPage::previewAll()
{
    const auto active = m_controller->profileStore()->activeProfile();
    if (!active) {
        setFeedback(QStringLiteral("没有当前订阅，无法预览"), true);
        return;
    }
    const QVector<ScriptStep> steps =
        m_controller->scriptManager()->resolveSteps(active->id);
    if (steps.isEmpty()) {
        setFeedback(QStringLiteral("当前订阅没有启用的脚本"), true);
        return;
    }
    ScriptTestDialog dialog(m_controller, steps,
                            QStringLiteral("预览 · %1").arg(active->name), window());
    dialog.exec();
}

void ScriptsPage::deleteScript(const QString& id)
{
    const auto script = m_controller->scriptManager()->find(id);
    if (!script)
        return;
    if (!ConfirmDialog::ask(window(), QStringLiteral("删除脚本"),
                            QStringLiteral("将删除脚本「%1」及其所有历史版本。"
                                           "订阅本身不会被修改。")
                                .arg(script->name),
                            QStringLiteral("删除"), true)) {
        return;
    }
    QString error;
    if (!m_controller->scriptManager()->removeScript(id, &error)) {
        setFeedback(error, true);
        return;
    }
    if (m_editor && m_editor->scriptId() == id)
        closeEditor();
    rebuild();
}

void ScriptsPage::checkUpdate(const QString& id)
{
    m_controller->scriptManager()->checkForUpdate(id, true);
    setFeedback(QStringLiteral("正在检查脚本更新…"), false);
    updateBusyState();
}

void ScriptsPage::openBindings(const QString& id)
{
    ScriptBindingsDialog dialog(m_controller, id, window());
    if (dialog.exec() == QDialog::Accepted)
        setFeedback(QStringLiteral("关联已更新"), false);
}

void ScriptsPage::openHistory(const QString& id)
{
    ScriptHistoryDialog dialog(m_controller->scriptManager(), id, window());
    dialog.exec();
}

void ScriptsPage::openLogs(const QString& id)
{
    ScriptLogsDialog dialog(m_controller->scriptManager(), id, window());
    dialog.exec();
}

void ScriptsPage::testScript(const QString& id)
{
    const auto script = m_controller->scriptManager()->find(id);
    if (!script)
        return;
    QString error;
    const QByteArray source = m_controller->scriptManager()->content(id, &error);
    if (!error.isEmpty()) {
        setFeedback(error, true);
        return;
    }
    ScriptTestDialog dialog(m_controller, script->name, source, window());
    dialog.exec();
}

void ScriptsPage::setFeedback(const QString& message, bool error)
{
    m_feedback->setText(message);
    m_feedback->setStyleSheet(
        QStringLiteral("color: %1;").arg((error ? pal().error : pal().primary).name()));
}
