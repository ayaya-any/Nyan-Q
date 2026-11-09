#include "ui/editor/ScriptEditorView.h"

#include "app/Fonts.h"
#include "app/Theme.h"
#include "core/ClashController.h"
#include "lang/LanguageRegistry.h"
#include "scripts/ScriptManager.h"
#include "ui/dialogs/ConfirmDialog.h"
#include "ui/dialogs/ScriptDialogs.h"
#include "ui/editor/CodeEditor.h"
#include "ui/widgets/MaterialWidgets.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

using namespace nq::scripts;

namespace {

class ScriptEditorChrome final : public QWidget {
public:
    enum class Edge { None, Right, Bottom };

    explicit ScriptEditorChrome(Edge edge, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_edge(edge)
    {
        setAttribute(Qt::WA_StyledBackground, false);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const Palette& p = pal();
        QPainter painter(this);
        painter.fillRect(rect(), p.surfaceContainer);
        painter.setPen(p.outlineVariant);
        if (m_edge == Edge::Right)
            painter.drawLine(width() - 1, 0, width() - 1, height());
        else if (m_edge == Edge::Bottom)
            painter.drawLine(0, height() - 1, width(), height() - 1);
    }

private:
    Edge m_edge;
};

QLabel* microLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "micro");
    label->setWordWrap(true);
    return label;
}

QString formatTime(const QDateTime& value)
{
    return value.isValid() ? value.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                           : QStringLiteral("从未");
}

}  // namespace

ScriptEditorView::ScriptEditorView(ClashController* controller, const QString& scriptId,
                                   QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_scriptId(scriptId)
{
    setObjectName(QStringLiteral("scriptEditorView"));
    setAttribute(Qt::WA_StyledBackground, false);
    buildUi();
    wire();
    loadContent();
}

ScriptEditorView::~ScriptEditorView() = default;

void ScriptEditorView::buildUi()
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* side = new ScriptEditorChrome(ScriptEditorChrome::Edge::Right, this);
    side->setFixedWidth(292);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(18, 14, 16, 14);
    sideLayout->setSpacing(8);

    auto* topRow = new QHBoxLayout;
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(7);
    auto* back = new M3IconButton(QStringLiteral("arrow_back"), side);
    back->setDiameter(32);
    back->setGlyphSize(17);
    back->setToolTip(QStringLiteral("返回脚本列表"));
    topRow->addWidget(back);
    auto* title = microLabel(QStringLiteral("脚本编辑器"), side);
    topRow->addWidget(title);
    topRow->addStretch(1);
    sideLayout->addLayout(topRow);

    m_nameEdit = new QLineEdit(side);
    m_nameEdit->setObjectName(QStringLiteral("editorName"));
    m_nameEdit->setFont(Fonts::text(19, QFont::Bold));
    m_nameEdit->setMaxLength(80);
    m_nameEdit->setToolTip(QStringLiteral("修改脚本名称，按 Enter 保存"));
    sideLayout->addWidget(m_nameEdit);

    auto* pills = new QHBoxLayout;
    pills->setContentsMargins(0, 0, 0, 0);
    pills->setSpacing(5);
    m_sourcePill = new M3Pill(QStringLiteral("本地"), side);
    m_sourcePill->setTone(M3Pill::Tone::Neutral);
    pills->addWidget(m_sourcePill);
    m_trustPill = new M3Pill(QStringLiteral("本地脚本"), side);
    m_trustPill->setTone(M3Pill::Tone::Neutral);
    pills->addWidget(m_trustPill);
    pills->addStretch();
    sideLayout->addLayout(pills);

    m_urlLabel = microLabel(QString(), side);
    m_urlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sideLayout->addWidget(m_urlLabel);
    m_hashLabel = microLabel(QString(), side);
    m_hashLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sideLayout->addWidget(m_hashLabel);
    m_updatedLabel = microLabel(QString(), side);
    sideLayout->addWidget(m_updatedLabel);
    m_checkedLabel = microLabel(QString(), side);
    sideLayout->addWidget(m_checkedLabel);

    sideLayout->addWidget(new M3Divider(side));

    auto* enabledRow = new QHBoxLayout;
    enabledRow->setContentsMargins(0, 0, 0, 0);
    enabledRow->addWidget(microLabel(QStringLiteral("启用脚本"), side));
    enabledRow->addStretch();
    m_enabledSwitch = new M3Switch(side);
    enabledRow->addWidget(m_enabledSwitch);
    sideLayout->addLayout(enabledRow);

    auto* autoRow = new QHBoxLayout;
    autoRow->setContentsMargins(0, 0, 0, 0);
    autoRow->addWidget(microLabel(QStringLiteral("自动检查更新"), side));
    autoRow->addStretch();
    m_autoUpdateSwitch = new M3Switch(side);
    autoRow->addWidget(m_autoUpdateSwitch);
    sideLayout->addLayout(autoRow);

    auto* intervalRow = new QHBoxLayout;
    intervalRow->setContentsMargins(0, 0, 0, 0);
    intervalRow->addWidget(microLabel(QStringLiteral("检查间隔"), side));
    intervalRow->addStretch();
    m_interval = new QSpinBox(side);
    m_interval->setRange(1, 24 * 30);
    m_interval->setSuffix(QStringLiteral(" 小时"));
    m_interval->setFixedWidth(100);
    intervalRow->addWidget(m_interval);
    sideLayout->addLayout(intervalRow);

    sideLayout->addWidget(microLabel(QStringLiteral("预期 SHA-256（可选）"), side));
    m_expectedHash = new QLineEdit(side);
    m_expectedHash->setPlaceholderText(QStringLiteral("64 位十六进制；留空表示不固定"));
    m_expectedHash->setClearButtonEnabled(true);
    sideLayout->addWidget(m_expectedHash);

    sideLayout->addWidget(new M3Divider(side));

    m_saveButton = new M3Button(QStringLiteral("保存脚本"), M3Button::Variant::Filled, side);
    m_saveButton->setIconText(QStringLiteral("save"));
    sideLayout->addWidget(m_saveButton);
    auto* test = new M3Button(QStringLiteral("测试脚本"), M3Button::Variant::Tonal, side);
    test->setIconText(QStringLiteral("play_arrow"));
    sideLayout->addWidget(test);
    auto* bindings = new M3Button(QStringLiteral("关联订阅"), M3Button::Variant::Text, side);
    bindings->setIconText(QStringLiteral("link"));
    sideLayout->addWidget(bindings);
    auto* history = new M3Button(QStringLiteral("历史版本"), M3Button::Variant::Text, side);
    history->setIconText(QStringLiteral("history"));
    sideLayout->addWidget(history);
    auto* logs = new M3Button(QStringLiteral("运行日志"), M3Button::Variant::Text, side);
    logs->setIconText(QStringLiteral("terminal"));
    sideLayout->addWidget(logs);
    m_updateButton = new M3Button(QStringLiteral("检查更新"), M3Button::Variant::Text, side);
    m_updateButton->setIconText(QStringLiteral("refresh"));
    sideLayout->addWidget(m_updateButton);

    sideLayout->addStretch(1);
    m_feedback = microLabel(QString(), side);
    sideLayout->addWidget(m_feedback);
    sideLayout->addWidget(microLabel(
        QStringLiteral("Ctrl+S 保存 · Ctrl+F 查找\nCtrl+W 关闭"), side));

    root->addWidget(side);

    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    auto* tabBar = new ScriptEditorChrome(ScriptEditorChrome::Edge::Bottom, right);
    tabBar->setFixedHeight(44);
    auto* tabLayout = new QHBoxLayout(tabBar);
    tabLayout->setContentsMargins(16, 0, 12, 0);
    tabLayout->setSpacing(8);
    auto* fileIcon = new M3IconLabel(QStringLiteral("code"), tabBar);
    fileIcon->setFixedBox(19, 19);
    fileIcon->setGlyphSize(16);
    tabLayout->addWidget(fileIcon);
    auto* tabName = new QLabel(QStringLiteral("main(config, profileName)"), tabBar);
    tabName->setFont(Fonts::text(13, QFont::DemiBold));
    tabLayout->addWidget(tabName);
    m_modifiedPill = new M3Pill(QStringLiteral("未保存"), tabBar);
    m_modifiedPill->setTone(M3Pill::Tone::Warning);
    m_modifiedPill->setDot(true);
    tabLayout->addWidget(m_modifiedPill);
    tabLayout->addStretch(1);
    auto* quickFind = new M3IconButton(QStringLiteral("search"), tabBar);
    quickFind->setDiameter(30);
    quickFind->setGlyphSize(16);
    quickFind->setToolTip(QStringLiteral("查找 (Ctrl+F)"));
    tabLayout->addWidget(quickFind);
    auto* quickSave = new M3IconButton(QStringLiteral("save"), tabBar);
    quickSave->setDiameter(30);
    quickSave->setGlyphSize(16);
    quickSave->setToolTip(QStringLiteral("保存 (Ctrl+S)"));
    tabLayout->addWidget(quickSave);
    rightLayout->addWidget(tabBar);

    m_findBar = new ScriptEditorChrome(ScriptEditorChrome::Edge::Bottom, right);
    m_findBar->setFixedHeight(44);
    auto* findLayout = new QHBoxLayout(m_findBar);
    findLayout->setContentsMargins(16, 5, 10, 6);
    findLayout->setSpacing(6);
    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->setObjectName(QStringLiteral("editorField"));
    m_findEdit->setPlaceholderText(QStringLiteral("查找"));
    findLayout->addWidget(m_findEdit, 1);
    auto* prev = new M3IconButton(QStringLiteral("keyboard_arrow_up"), m_findBar);
    prev->setDiameter(28);
    prev->setGlyphSize(16);
    prev->setToolTip(QStringLiteral("上一个 (Shift+F3)"));
    findLayout->addWidget(prev);
    auto* next = new M3IconButton(QStringLiteral("keyboard_arrow_down"), m_findBar);
    next->setDiameter(28);
    next->setGlyphSize(16);
    next->setToolTip(QStringLiteral("下一个 (F3)"));
    findLayout->addWidget(next);
    auto* closeFind = new M3IconButton(QStringLiteral("close"), m_findBar);
    closeFind->setDiameter(28);
    closeFind->setGlyphSize(15);
    closeFind->setToolTip(QStringLiteral("关闭查找"));
    findLayout->addWidget(closeFind);
    m_findBar->hide();
    rightLayout->addWidget(m_findBar);

    m_editor = new CodeEditor(right);
    m_editor->setLanguageProvider(
        nq::lang::LanguageRegistry::instance().providerById(QStringLiteral("javascript")));
    rightLayout->addWidget(m_editor, 1);

    auto* status = new ScriptEditorChrome(ScriptEditorChrome::Edge::None, right);
    status->setFixedHeight(28);
    auto* statusLayout = new QHBoxLayout(status);
    statusLayout->setContentsMargins(16, 0, 12, 0);
    statusLayout->setSpacing(14);
    m_cursorLabel = microLabel(QStringLiteral("行 1，列 1"), status);
    statusLayout->addWidget(m_cursorLabel);
    m_statsLabel = microLabel(QString(), status);
    statusLayout->addWidget(m_statsLabel);
    statusLayout->addStretch();
    statusLayout->addWidget(microLabel(QStringLiteral("UTF-8 · JavaScript"), status));
    rightLayout->addWidget(status);

    root->addWidget(right, 1);

    connect(back, &QAbstractButton::clicked, this, [this] {
        if (confirmClose())
            emit closeRequested();
    });
    connect(m_saveButton, &QAbstractButton::clicked, this, &ScriptEditorView::saveContent);
    connect(quickSave, &QAbstractButton::clicked, this, &ScriptEditorView::saveContent);
    connect(quickFind, &QAbstractButton::clicked, this, &ScriptEditorView::showFindBar);
    connect(test, &QAbstractButton::clicked, this, &ScriptEditorView::testScript);
    connect(bindings, &QAbstractButton::clicked, this, &ScriptEditorView::openBindings);
    connect(history, &QAbstractButton::clicked, this, &ScriptEditorView::openHistory);
    connect(logs, &QAbstractButton::clicked, this, &ScriptEditorView::openLogs);
    connect(m_updateButton, &QAbstractButton::clicked, this, &ScriptEditorView::checkUpdate);
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &ScriptEditorView::commitName);
    connect(m_expectedHash, &QLineEdit::editingFinished, this,
            &ScriptEditorView::applyExpectedHash);
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this] { findNext(false); });
    connect(prev, &QAbstractButton::clicked, this, [this] { findNext(true); });
    connect(next, &QAbstractButton::clicked, this, [this] { findNext(false); });
    connect(closeFind, &QAbstractButton::clicked, this, &ScriptEditorView::hideFindBar);
    connect(m_enabledSwitch, &QAbstractButton::toggled, this, [this](bool checked) {
        if (m_loading)
            return;
        QString error;
        if (!m_controller->scriptManager()->setEnabled(m_scriptId, checked, &error)) {
            setFeedback(error, true);
            refreshMeta();
            return;
        }
        setFeedback(checked ? QStringLiteral("脚本已启用") : QStringLiteral("脚本已停用"));
        refreshMeta();
    });
    connect(m_autoUpdateSwitch, &QAbstractButton::toggled, this, [this](bool checked) {
        if (m_loading)
            return;
        QString error;
        if (!m_controller->scriptManager()->setAutoUpdate(m_scriptId, checked, &error)) {
            setFeedback(error, true);
            refreshMeta();
            return;
        }
        setFeedback(checked ? QStringLiteral("已开启自动更新") : QStringLiteral("已关闭自动更新"));
        refreshMeta();
    });
    connect(m_interval, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int hours) {
        if (m_loading)
            return;
        QString error;
        if (!m_controller->scriptManager()->setUpdateInterval(m_scriptId, hours, &error))
            setFeedback(error, true);
    });
}

void ScriptEditorView::wire()
{
    connect(m_editor->document(), &QTextDocument::modificationChanged, this,
            [this](bool) { updateModifiedState(); });
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        updateModifiedState();
        m_statsLabel->setText(QStringLiteral("%1 行 · %2 字符")
                                  .arg(m_editor->document()->blockCount())
                                  .arg(m_editor->toPlainText().size()));
    });
    connect(m_editor, &CodeEditor::cursorInfoChanged, this, [this](int line, int column) {
        m_cursorLabel->setText(QStringLiteral("行 %1，列 %2").arg(line).arg(column));
    });
    connect(m_editor, &CodeEditor::searchRequested, this, &ScriptEditorView::showFindBar);

    auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
    saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(saveShortcut, &QShortcut::activated, this, &ScriptEditorView::saveContent);
    auto* closeShortcut = new QShortcut(QKeySequence::Close, this);
    closeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(closeShortcut, &QShortcut::activated, this, [this] {
        if (confirmClose())
            emit closeRequested();
    });
    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findShortcut, &QShortcut::activated, this, &ScriptEditorView::showFindBar);
    auto* nextShortcut = new QShortcut(QKeySequence(Qt::Key_F3), this);
    nextShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(nextShortcut, &QShortcut::activated, this, [this] { findNext(false); });
    auto* prevShortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3), this);
    prevShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(prevShortcut, &QShortcut::activated, this, [this] { findNext(true); });

    connect(m_controller->scriptManager(), &ScriptManager::updateFinished, this,
            [this](const ScriptUpdateResult& result) {
                if (result.scriptId != m_scriptId)
                    return;
                m_updateButton->setEnabled(true);
                if (result.ok && !result.notModified && !result.skipped &&
                    (!result.oldContent.isEmpty() || !result.newContent.isEmpty())) {
                    if (result.manual) {
                        ScriptUpdateDiffDialog dialog(result, window());
                        dialog.exec();
                    }
                    if (!isModified()) {
                        loadContent();
                    } else {
                        setFeedback(QStringLiteral("远程脚本已更新；保存当前编辑会覆盖新版本"),
                                    true);
                    }
                } else if (!result.ok) {
                    setFeedback(result.error, true);
                } else {
                    setFeedback(result.notModified ? QStringLiteral("已是最新版本")
                                                   : QStringLiteral("脚本内容没有变化"));
                }
                refreshMeta();
            });
}

void ScriptEditorView::loadContent()
{
    ScriptManager* manager = m_controller->scriptManager();
    const auto script = manager->find(m_scriptId);
    if (!script) {
        m_editor->setReadOnly(true);
        setFeedback(QStringLiteral("找不到脚本"), true);
        return;
    }
    QString error;
    const QByteArray data = manager->content(m_scriptId, &error);
    if (!error.isEmpty()) {
        m_editor->setReadOnly(true);
        setFeedback(error, true);
        return;
    }

    m_loading = true;
    m_editor->setPlainText(QString::fromUtf8(data));
    m_originalText = m_editor->toPlainText().toUtf8();
    QTextCursor cursor = m_editor->textCursor();
    cursor.movePosition(QTextCursor::Start);
    m_editor->setTextCursor(cursor);
    m_editor->document()->setModified(false);
    m_loading = false;
    m_statsLabel->setText(QStringLiteral("%1 行 · %2 字符")
                              .arg(m_editor->document()->blockCount())
                              .arg(m_editor->toPlainText().size()));
    m_editor->setFocus();
    refreshMeta();
    updateModifiedState();
}

void ScriptEditorView::refreshMeta()
{
    ScriptManager* manager = m_controller->scriptManager();
    const auto script = manager->find(m_scriptId);
    if (!script) {
        setFeedback(QStringLiteral("脚本已被删除"), true);
        return;
    }

    m_loading = true;
    if (!m_nameEdit->hasFocus())
        m_nameEdit->setText(script->name);
    m_sourcePill->setText(sourceKindLabel(script->kind));
    m_sourcePill->setTone(script->remote() ? M3Pill::Tone::Info : M3Pill::Tone::Neutral);
    const ScriptTrustState trust = trustState(*script);
    m_trustPill->setText(trustStateLabel(trust));
    switch (trust) {
    case ScriptTrustState::Pinned:
        m_trustPill->setTone(M3Pill::Tone::Success);
        break;
    case ScriptTrustState::PinMismatch:
        m_trustPill->setTone(M3Pill::Tone::Error);
        break;
    case ScriptTrustState::Untrusted:
        m_trustPill->setTone(M3Pill::Tone::Warning);
        break;
    case ScriptTrustState::Trusted:
        m_trustPill->setTone(M3Pill::Tone::Primary);
        break;
    case ScriptTrustState::Local:
        m_trustPill->setTone(M3Pill::Tone::Neutral);
        break;
    }

    if (script->remote())
        m_urlLabel->setText(QStringLiteral("来源：%1").arg(maskSecretUrl(script->url)));
    else
        m_urlLabel->setText(script->sourcePath.isEmpty()
                                ? QStringLiteral("来源：在 Nyan-Q 中创建")
                                : QStringLiteral("来源：%1").arg(script->sourcePath));
    m_urlLabel->setToolTip(script->remote() ? script->url : script->sourcePath);
    m_hashLabel->setText(QStringLiteral("SHA-256：%1").arg(script->clientSha256.left(24)));
    m_hashLabel->setToolTip(script->clientSha256);
    m_updatedLabel->setText(QStringLiteral("更新于 %1").arg(formatTime(script->updatedAt)));
    m_checkedLabel->setText(QStringLiteral("检查于 %1").arg(formatTime(script->lastCheckedAt)));

    m_enabledSwitch->setChecked(script->enabled);
    m_autoUpdateSwitch->setChecked(script->autoUpdate);
    m_autoUpdateSwitch->setEnabled(script->remote());
    m_interval->setValue(script->updateIntervalHours);
    m_interval->setEnabled(script->remote());
    m_updateButton->setVisible(script->remote());
    if (!m_expectedHash->hasFocus())
        m_expectedHash->setText(script->expectedSha256);
    m_loading = false;
}

void ScriptEditorView::saveContent()
{
    if (!m_editor || m_editor->isReadOnly())
        return;
    QByteArray content = m_editor->toPlainText().toUtf8();
    if (!content.endsWith('\n'))
        content.append('\n');
    QString error;
    if (!m_controller->scriptManager()->saveContent(m_scriptId, content, &error)) {
        setFeedback(error, true);
        return;
    }
    m_originalText = content;
    m_editor->document()->setModified(false);
    setFeedback(QStringLiteral("已保存"));
    refreshMeta();
    updateModifiedState();
    emit saved();
}

void ScriptEditorView::commitName()
{
    const auto script = m_controller->scriptManager()->find(m_scriptId);
    if (!script)
        return;
    const QString name = m_nameEdit->text().trimmed();
    if (name == script->name) {
        m_nameEdit->setText(script->name);
        return;
    }
    QString error;
    if (!m_controller->scriptManager()->renameScript(m_scriptId, name, &error)) {
        m_nameEdit->setText(script->name);
        setFeedback(error, true);
        return;
    }
    setFeedback(QStringLiteral("脚本名称已更新"));
    refreshMeta();
}

void ScriptEditorView::applyExpectedHash()
{
    QString error;
    if (!m_controller->scriptManager()->setExpectedSha256(
            m_scriptId, m_expectedHash->text().trimmed(), &error)) {
        setFeedback(error, true);
        refreshMeta();
        return;
    }
    setFeedback(m_expectedHash->text().trimmed().isEmpty()
                    ? QStringLiteral("已清除哈希固定")
                    : QStringLiteral("哈希固定值已保存"));
    refreshMeta();
}

void ScriptEditorView::testScript()
{
    QByteArray content = m_editor->toPlainText().toUtf8();
    if (!content.endsWith('\n'))
        content.append('\n');
    ScriptTestDialog dialog(m_controller, m_nameEdit->text(), content, window());
    dialog.exec();
}

void ScriptEditorView::openBindings()
{
    ScriptBindingsDialog dialog(m_controller, m_scriptId, window());
    if (dialog.exec() == QDialog::Accepted)
        setFeedback(QStringLiteral("关联已更新，当前配置将重新生成"));
}

void ScriptEditorView::openHistory()
{
    ScriptHistoryDialog dialog(m_controller->scriptManager(), m_scriptId, window());
    dialog.exec();
    loadContent();
}

void ScriptEditorView::openLogs()
{
    ScriptLogsDialog dialog(m_controller->scriptManager(), m_scriptId, window());
    dialog.exec();
}

void ScriptEditorView::checkUpdate()
{
    m_updateButton->setEnabled(false);
    setFeedback(QStringLiteral("正在检查远程脚本…"));
    m_controller->scriptManager()->checkForUpdate(m_scriptId, true);
}

void ScriptEditorView::updateModifiedState()
{
    if (!m_editor)
        return;
    const bool modified = isModified();
    m_modifiedPill->setVisible(modified);
    m_saveButton->setEnabled(modified);
}

bool ScriptEditorView::isModified() const
{
    return m_editor && m_editor->document()->isModified();
}

bool ScriptEditorView::confirmClose()
{
    if (!isModified())
        return true;
    return ConfirmDialog::ask(
        this, QStringLiteral("未保存的脚本"),
        QStringLiteral("当前脚本还有未保存的修改。关闭编辑器将丢弃这些修改。"),
        QStringLiteral("丢弃并关闭"), true);
}

void ScriptEditorView::setFeedback(const QString& message, bool error)
{
    m_feedback->setText(message);
    m_feedback->setStyleSheet(
        QStringLiteral("color: %1;").arg((error ? pal().error : pal().primary).name()));
}

void ScriptEditorView::showFindBar()
{
    m_findBar->setVisible(true);
    const QString selected = m_editor->textCursor().selectedText();
    if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator))
        m_findEdit->setText(selected);
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void ScriptEditorView::hideFindBar()
{
    m_findBar->hide();
    m_editor->setFocus();
}

void ScriptEditorView::findNext(bool backward)
{
    m_findBar->setVisible(true);
    const QString needle = m_findEdit->text();
    if (needle.isEmpty())
        return;
    QTextDocument::FindFlags flags;
    if (backward)
        flags |= QTextDocument::FindBackward;
    QTextCursor found = m_editor->document()->find(needle, m_editor->textCursor(), flags);
    if (found.isNull()) {
        QTextCursor start = m_editor->textCursor();
        start.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        found = m_editor->document()->find(needle, start, flags);
    }
    if (found.isNull()) {
        setFeedback(QStringLiteral("未找到「%1」").arg(needle), true);
        return;
    }
    m_editor->setTextCursor(found);
    m_editor->centerCursor();
}

void ScriptEditorView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_findBar->isVisible()) {
            hideFindBar();
            event->accept();
            return;
        }
        if (confirmClose()) {
            emit closeRequested();
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void ScriptEditorView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), pal().background);
}
