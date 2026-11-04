#include "ui/editor/ConfigEditorView.h"

#include "app/Fonts.h"
#include "app/Theme.h"
#include "core/ClashController.h"
#include "lsp/LspClient.h"
#include "lsp/LspServerRegistry.h"
#include "ui/dialogs/ConfirmDialog.h"
#include "ui/editor/CodeEditor.h"
#include "ui/widgets/MaterialWidgets.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QShortcut>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

namespace {

class EditorChrome final : public QWidget {
public:
    enum class Edge { None, Right, Bottom };

    explicit EditorChrome(Edge edge, QWidget* parent = nullptr)
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

QLabel* captionLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "micro");
    return label;
}

QLineEdit* editorField(const QString& placeholder, QWidget* parent)
{
    auto* field = new QLineEdit(parent);
    field->setObjectName(QStringLiteral("editorField"));
    field->setPlaceholderText(placeholder);
    return field;
}

}  // namespace

ConfigEditorView::ConfigEditorView(ClashController* controller,
                                   const cq::ProfileMeta& profile, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_profile(profile)
{
    setObjectName(QStringLiteral("configEditorView"));
    setAttribute(Qt::WA_StyledBackground, false);
    if (m_controller && m_controller->profileStore())
        m_filePath = m_controller->profileStore()->configPath(profile);

    buildUi();
    wire();
    loadContent();
    startLanguageServer();
}

ConfigEditorView::~ConfigEditorView()
{
    if (m_lsp) {
        disconnect(m_lsp, nullptr, this, nullptr);
        m_lsp->stop();
        m_lsp = nullptr;
    }
}

void ConfigEditorView::buildUi()
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* side = new EditorChrome(EditorChrome::Edge::Right, this);
    side->setFixedWidth(274);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(18, 14, 16, 14);
    sideLayout->setSpacing(8);

    auto* topRow = new QHBoxLayout;
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(7);
    auto* back = new M3IconButton(QStringLiteral("arrow_back"), side);
    back->setDiameter(32);
    back->setGlyphSize(17);
    back->setToolTip(QStringLiteral("返回配置页"));
    topRow->addWidget(back);
    auto* editorTitle = new QLabel(QStringLiteral("配置编辑器"), side);
    editorTitle->setProperty("role", "micro");
    topRow->addWidget(editorTitle);
    topRow->addStretch(1);
    sideLayout->addLayout(topRow);

    m_nameEdit = new QLineEdit(m_profile.name, side);
    m_nameEdit->setObjectName(QStringLiteral("editorName"));
    m_nameEdit->setFont(Fonts::text(19, QFont::Bold));
    m_nameEdit->setMaxLength(80);
    m_nameEdit->setClearButtonEnabled(false);
    m_nameEdit->setToolTip(QStringLiteral("点击修改配置名称，按 Enter 保存"));
    auto* nameRow = new QHBoxLayout;
    nameRow->setContentsMargins(0, 0, 0, 0);
    nameRow->setSpacing(4);
    nameRow->addWidget(m_nameEdit, 1);
    auto* rename = new M3IconButton(QStringLiteral("edit"), side);
    rename->setDiameter(26);
    rename->setGlyphSize(14);
    rename->setToolTip(QStringLiteral("重命名配置"));
    nameRow->addWidget(rename, 0, Qt::AlignVCenter);
    sideLayout->addLayout(nameRow);
    connect(rename, &QAbstractButton::clicked, this, [this] {
        m_nameEdit->setFocus();
        m_nameEdit->selectAll();
    });

    auto* pills = new QHBoxLayout;
    pills->setContentsMargins(0, 0, 0, 0);
    pills->setSpacing(6);
    auto* typePill = new M3Pill(cq::profileTypeLabel(m_profile.type), side);
    typePill->setTone(M3Pill::Tone::Info);
    pills->addWidget(typePill);
    if (m_profile.active) {
        auto* activePill = new M3Pill(QStringLiteral("当前配置"), side);
        activePill->setTone(M3Pill::Tone::Success);
        activePill->setDot(true);
        pills->addWidget(activePill);
    }
    pills->addStretch(1);
    sideLayout->addLayout(pills);

    m_fileName = new QLabel(m_profile.fileName, side);
    m_fileName->setProperty("role", "micro");
    m_fileName->setWordWrap(true);
    sideLayout->addWidget(m_fileName);

    sideLayout->addWidget(new M3Divider(side));

    m_saveButton = new M3Button(QStringLiteral("保存配置"), M3Button::Variant::Filled, side);
    m_saveButton->setIconText(QStringLiteral("save"));
    m_saveButton->setToolTip(QStringLiteral("保存并应用配置 (Ctrl+S)"));
    sideLayout->addWidget(m_saveButton);

    m_revertButton = new M3Button(QStringLiteral("还原修改"), M3Button::Variant::Text, side);
    m_revertButton->setIconText(QStringLiteral("history"));
    sideLayout->addWidget(m_revertButton);

    sideLayout->addStretch(1);
    m_feedback = new QLabel(side);
    m_feedback->setProperty("role", "micro");
    m_feedback->setWordWrap(true);
    sideLayout->addWidget(m_feedback);

    auto* shortcuts = captionLabel(
        QStringLiteral("Ctrl+S 保存 · Ctrl+F 查找\nCtrl+Space 提示 · Ctrl+W 关闭"), side);
    shortcuts->setWordWrap(true);
    sideLayout->addWidget(shortcuts);

    root->addWidget(side);

    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    auto* tabBar = new EditorChrome(EditorChrome::Edge::Bottom, right);
    tabBar->setFixedHeight(44);
    auto* tabLayout = new QHBoxLayout(tabBar);
    tabLayout->setContentsMargins(16, 0, 12, 0);
    tabLayout->setSpacing(8);
    auto* fileIcon = new M3IconLabel(QStringLiteral("description"), tabBar);
    fileIcon->setFixedBox(19, 19);
    fileIcon->setGlyphSize(16);
    tabLayout->addWidget(fileIcon);
    auto* tabName = new QLabel(m_profile.fileName, tabBar);
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

    m_findBar = new EditorChrome(EditorChrome::Edge::Bottom, right);
    m_findBar->setFixedHeight(46);
    auto* findLayout = new QHBoxLayout(m_findBar);
    findLayout->setContentsMargins(16, 5, 10, 6);
    findLayout->setSpacing(6);
    m_findEdit = editorField(QStringLiteral("查找"), m_findBar);
    findLayout->addWidget(m_findEdit, 2);
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
    m_replaceEdit = editorField(QStringLiteral("替换为"), m_findBar);
    findLayout->addWidget(m_replaceEdit, 2);
    auto* replace = new M3Button(QStringLiteral("替换"), M3Button::Variant::Tonal, m_findBar);
    replace->setCompact(true);
    findLayout->addWidget(replace);
    auto* replaceAllButton =
        new M3Button(QStringLiteral("全部"), M3Button::Variant::Text, m_findBar);
    replaceAllButton->setCompact(true);
    findLayout->addWidget(replaceAllButton);
    auto* closeFind = new M3IconButton(QStringLiteral("close"), m_findBar);
    closeFind->setDiameter(28);
    closeFind->setGlyphSize(15);
    closeFind->setToolTip(QStringLiteral("关闭查找"));
    findLayout->addWidget(closeFind);
    m_findBar->hide();
    rightLayout->addWidget(m_findBar);

    m_editor = new CodeEditor(right);
    rightLayout->addWidget(m_editor, 1);

    auto* status = new EditorChrome(EditorChrome::Edge::None, right);
    status->setFixedHeight(28);
    auto* statusLayout = new QHBoxLayout(status);
    statusLayout->setContentsMargins(16, 0, 12, 0);
    statusLayout->setSpacing(14);
    m_cursorLabel = new QLabel(QStringLiteral("行 1，列 1"), status);
    m_cursorLabel->setProperty("role", "micro");
    statusLayout->addWidget(m_cursorLabel);
    m_statsLabel = new QLabel(status);
    m_statsLabel->setProperty("role", "micro");
    statusLayout->addWidget(m_statsLabel);
    m_issueLabel = new QLabel(status);
    m_issueLabel->setProperty("role", "micro");
    statusLayout->addWidget(m_issueLabel);
    statusLayout->addStretch(1);
    auto* encoding = new QLabel(QStringLiteral("UTF-8"), status);
    encoding->setProperty("role", "micro");
    statusLayout->addWidget(encoding);
    auto* lineEnding = new QLabel(QStringLiteral("LF"), status);
    lineEnding->setProperty("role", "micro");
    statusLayout->addWidget(lineEnding);
    rightLayout->addWidget(status);

    root->addWidget(right, 1);

    connect(back, &QAbstractButton::clicked, this, [this] {
        if (confirmClose())
            emit closeRequested();
    });
    connect(m_saveButton, &QAbstractButton::clicked, this, &ConfigEditorView::saveContent);
    connect(quickSave, &QAbstractButton::clicked, this, &ConfigEditorView::saveContent);
    connect(m_revertButton, &QAbstractButton::clicked, this, &ConfigEditorView::revertContent);
    connect(quickFind, &QAbstractButton::clicked, this, [this] { showFindBar(false); });
    connect(prev, &QAbstractButton::clicked, this, [this] { findNext(true); });
    connect(next, &QAbstractButton::clicked, this, [this] { findNext(false); });
    connect(replace, &QAbstractButton::clicked, this, &ConfigEditorView::replaceOne);
    connect(replaceAllButton, &QAbstractButton::clicked, this, &ConfigEditorView::replaceAll);
    connect(closeFind, &QAbstractButton::clicked, this, &ConfigEditorView::hideFindBar);
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this] { findNext(false); });
    connect(m_replaceEdit, &QLineEdit::returnPressed, this, &ConfigEditorView::replaceOne);
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &ConfigEditorView::commitName);
    connect(m_editor, &CodeEditor::searchRequested, this, [this] { showFindBar(false); });
    connect(m_editor, &CodeEditor::replaceRequested, this, [this] { showFindBar(true); });
    connect(m_editor, &CodeEditor::formatRequested, this, &ConfigEditorView::formatContent);
    connect(m_editor, &CodeEditor::cursorInfoChanged, this,
            &ConfigEditorView::updateCursorStatus);
}

void ConfigEditorView::wire()
{
    m_changeTimer = new QTimer(this);
    m_changeTimer->setSingleShot(true);
    m_changeTimer->setInterval(140);
    connect(m_changeTimer, &QTimer::timeout, this, [this] {
        if (m_lsp && m_lsp->isReady() && !m_loading)
            m_lsp->changeDocument(m_filePath, m_editor->toPlainText());
    });

    connect(m_editor->document(), &QTextDocument::modificationChanged, this,
            [this](bool) { updateModifiedState(); });
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        updateModifiedState();
        m_statsLabel->setText(QStringLiteral("%1 行 · %2 字符")
                                  .arg(m_editor->document()->blockCount())
                                  .arg(m_editor->toPlainText().size()));
        if (!m_loading && m_lsp && m_lsp->isReady())
            m_changeTimer->start();
    });
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        m_editor->update();
        update();
    });

    connect(m_editor, &CodeEditor::completionRequested, this,
            [this](int line, int character) {
                if (!m_lsp || !m_lsp->isReady())
                    return;
                m_completionRequest = m_lsp->requestCompletion(m_filePath, line, character);
            });

    auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
    saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(saveShortcut, &QShortcut::activated, this, &ConfigEditorView::saveContent);

    auto* closeShortcut = new QShortcut(QKeySequence::Close, this);
    closeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(closeShortcut, &QShortcut::activated, this, [this] {
        if (confirmClose())
            emit closeRequested();
    });

    auto* nextShortcut = new QShortcut(QKeySequence(Qt::Key_F3), this);
    nextShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(nextShortcut, &QShortcut::activated, this, [this] { findNext(false); });

    auto* prevShortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3), this);
    prevShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(prevShortcut, &QShortcut::activated, this, [this] { findNext(true); });

    m_lsp = new LspClient(this);
    connect(m_lsp, &LspClient::ready, this, [this] {
        if (!m_lsp || m_filePath.isEmpty())
            return;
        m_lsp->openDocument(m_filePath, m_editor->toPlainText());
    });
    connect(m_lsp, &LspClient::errorMessage, this, [this](const QString& message) {
        setFeedback(message, true);
    });
    connect(m_lsp, &LspClient::logMessage, this, [this](const QString& message) {
        if (message.contains(QStringLiteral("error"), Qt::CaseInsensitive))
            setFeedback(message, true);
    });
    connect(m_lsp, &LspClient::diagnosticsReady, this,
            [this](const QString& path, const QVector<LspDiagnostic>& diagnostics) {
                if (QFileInfo(path).absoluteFilePath() !=
                    QFileInfo(m_filePath).absoluteFilePath())
                    return;
                m_editor->setDiagnostics(diagnostics);
                int errors = 0;
                int warnings = 0;
                for (const LspDiagnostic& diagnostic : diagnostics) {
                    if (diagnostic.severity == 1)
                        ++errors;
                    else if (diagnostic.severity == 2)
                        ++warnings;
                }
                if (diagnostics.isEmpty()) {
                    m_issueLabel->setText(QStringLiteral("无诊断"));
                    m_issueLabel->setStyleSheet(
                        QStringLiteral("color: %1;").arg(pal().onSurfaceVariant.name()));
                } else {
                    m_issueLabel->setText(QStringLiteral("%1 错误 · %2 警告")
                                              .arg(errors)
                                              .arg(warnings));
                    m_issueLabel->setStyleSheet(
                        QStringLiteral("color: %1;")
                            .arg((errors > 0 ? pal().error : pal().warning).name()));
                }
            });
    connect(m_lsp, &LspClient::completionReady, this,
            [this](int requestId, const QVector<LspCompletion>& completions) {
                if (requestId != m_completionRequest)
                    return;
                m_editor->showCompletions(completions);
            });
    connect(m_lsp, &LspClient::formattingReady, this,
            [this](int requestId, const QVector<LspTextEdit>& edits) {
                if (requestId != m_formatRequest)
                    return;
                if (edits.isEmpty()) {
                    setFeedback(QStringLiteral("LSP 认为当前格式无需调整"));
                    return;
                }
                m_editor->applyLspEdits(edits);
                setFeedback(QStringLiteral("已由 %1 格式化").arg(m_serverProfile.name));
            });
}

void ConfigEditorView::startLanguageServer()
{
    if (!m_lsp)
        return;
    LspServerProfile profile = m_serverProfile;
    if (profile.id.isEmpty()) {
        profile = LspServerRegistry::instance().profileForPath(m_filePath);
        m_serverProfile = profile;
    }
    if (profile.id.isEmpty()) {
        setFeedback(QStringLiteral("没有可用的语言服务"), true);
        return;
    }
    m_lsp->start(profile, QFileInfo(m_filePath).absolutePath());
}

void ConfigEditorView::loadContent()
{
    if (!m_controller || !m_controller->profileStore()) {
        m_readError = true;
        m_editor->setReadOnly(true);
        setFeedback(QStringLiteral("配置存储不可用"), true);
        updateModifiedState();
        return;
    }

    QString error;
    const QByteArray data =
        m_controller->profileStore()->configContent(m_profile.id, &error);
    if (!error.isEmpty()) {
        m_readError = true;
        m_editor->setReadOnly(true);
        m_editor->setPlaceholderText(QStringLiteral("无法读取配置文件"));
        setFeedback(error, true);
        updateModifiedState();
        return;
    }

    m_loading = true;
    m_editor->setPlainText(QString::fromUtf8(data));
    m_originalText = m_editor->toPlainText();
    QTextCursor cursor = m_editor->textCursor();
    cursor.movePosition(QTextCursor::Start);
    m_editor->setTextCursor(cursor);
    m_editor->document()->setModified(false);
    m_loading = false;
    m_statsLabel->setText(QStringLiteral("%1 行 · %2 字符")
                              .arg(m_editor->document()->blockCount())
                              .arg(m_editor->toPlainText().size()));
    updateModifiedState();

    m_serverProfile = LspServerRegistry::instance().profileForPath(m_filePath);
    if (m_serverProfile.id.isEmpty())
        m_serverProfile = LspServerRegistry::instance().defaultProfile();
    m_editor->setFocus();
}

void ConfigEditorView::saveContent()
{
    if (!m_controller || !m_controller->profileStore() || m_readError || !isModified())
        return;
    QByteArray content = m_editor->toPlainText().toUtf8();
    if (!content.endsWith('\n'))
        content.append('\n');

    QString error;
    if (!m_controller->profileStore()->saveConfigContent(m_profile.id, content, &error)) {
        setFeedback(error, true);
        return;
    }
    if (m_lsp && m_lsp->isReady()) {
        m_changeTimer->stop();
        m_lsp->changeDocument(m_filePath, m_editor->toPlainText());
        m_lsp->saveDocument(m_filePath);
    }
    m_originalText = m_editor->toPlainText();
    m_editor->document()->setModified(false);
    setFeedback(m_profile.active ? QStringLiteral("已保存 · 正在热重载内核配置")
                                 : QStringLiteral("已保存"));
    updateModifiedState();
    emit saved();
}

void ConfigEditorView::revertContent()
{
    if (!isModified())
        return;
    if (!ConfirmDialog::ask(this, QStringLiteral("还原配置"),
                            QStringLiteral("将丢弃当前所有未保存的修改，恢复为打开编辑器时的内容。"),
                            QStringLiteral("还原"), true)) {
        return;
    }
    m_loading = true;
    m_editor->setPlainText(m_originalText);
    QTextCursor cursor = m_editor->textCursor();
    cursor.movePosition(QTextCursor::Start);
    m_editor->setTextCursor(cursor);
    m_editor->document()->setModified(false);
    m_loading = false;
    if (m_lsp && m_lsp->isReady())
        m_lsp->changeDocument(m_filePath, m_editor->toPlainText());
    setFeedback(QStringLiteral("已还原到打开时的内容"));
    updateModifiedState();
}

void ConfigEditorView::formatContent()
{
    if (m_readError)
        return;
    if (!m_lsp || !m_lsp->isReady()) {
        setFeedback(QStringLiteral("LSP 尚未就绪，暂时无法格式化"), true);
        return;
    }
    m_changeTimer->stop();
    m_lsp->changeDocument(m_filePath, m_editor->toPlainText());
    m_formatRequest = m_lsp->requestFormatting(m_filePath);
    if (m_formatRequest < 0)
        setFeedback(QStringLiteral("LSP 格式化请求失败"), true);
}

void ConfigEditorView::updateModifiedState()
{
    if (!m_editor)
        return;
    const bool modified = isModified();
    m_modifiedPill->setVisible(modified);
    m_saveButton->setEnabled(modified && !m_readError);
    m_revertButton->setEnabled(modified && !m_readError);
}

void ConfigEditorView::updateCursorStatus(int line, int column)
{
    m_cursorLabel->setText(QStringLiteral("行 %1，列 %2").arg(line).arg(column));
}

void ConfigEditorView::showFindBar(bool replace)
{
    m_findBar->setVisible(true);
    m_replaceEdit->setVisible(replace);
    for (QWidget* widget : m_findBar->findChildren<QWidget*>()) {
        if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
            if (button->text() == QStringLiteral("替换") ||
                button->text() == QStringLiteral("全部"))
                button->setVisible(replace);
        }
    }
    const QString selected = m_editor->textCursor().selectedText();
    if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator))
        m_findEdit->setText(selected);
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void ConfigEditorView::hideFindBar()
{
    m_findBar->hide();
    m_editor->setFocus();
}

void ConfigEditorView::findNext(bool backward)
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

void ConfigEditorView::replaceOne()
{
    const QString needle = m_findEdit->text();
    if (needle.isEmpty())
        return;
    QTextCursor cursor = m_editor->textCursor();
    if (cursor.hasSelection() &&
        cursor.selectedText().compare(needle, Qt::CaseInsensitive) == 0) {
        cursor.insertText(m_replaceEdit->text());
        m_editor->setTextCursor(cursor);
    }
    findNext(false);
}

void ConfigEditorView::replaceAll()
{
    const QString needle = m_findEdit->text();
    if (needle.isEmpty())
        return;
    const QString replacement = m_replaceEdit->text();
    QTextCursor cursor(m_editor->document());
    cursor.beginEditBlock();
    int count = 0;
    while (true) {
        QTextCursor found = m_editor->document()->find(needle, cursor);
        if (found.isNull())
            break;
        found.insertText(replacement);
        cursor = found;
        ++count;
    }
    cursor.endEditBlock();
    if (count > 0) {
        m_editor->document()->setModified(true);
        setFeedback(QStringLiteral("已替换 %1 处").arg(count));
    } else {
        setFeedback(QStringLiteral("未找到「%1」").arg(needle), true);
    }
}

void ConfigEditorView::commitName()
{
    if (!m_controller || !m_controller->profileStore() || !m_nameEdit)
        return;
    const QString name = m_nameEdit->text().trimmed();
    if (name == m_profile.name) {
        m_nameEdit->setText(m_profile.name);
        return;
    }
    QString error;
    if (!m_controller->profileStore()->renameProfile(m_profile.id, name, &error)) {
        m_nameEdit->setText(m_profile.name);
        setFeedback(error, true);
        return;
    }
    m_profile.name = name;
    m_nameEdit->setText(name);
    setFeedback(QStringLiteral("配置名称已更新"));
    emit profileChanged();
}

void ConfigEditorView::setFeedback(const QString& text, bool error)
{
    m_feedback->setText(text);
    m_feedback->setStyleSheet(
        QStringLiteral("color: %1;").arg((error ? pal().error : pal().primary).name()));
}

bool ConfigEditorView::isModified() const
{
    return m_editor && m_editor->document()->isModified();
}

bool ConfigEditorView::confirmClose()
{
    if (!isModified())
        return true;
    return ConfirmDialog::ask(
        this, QStringLiteral("未保存的修改"),
        QStringLiteral("当前配置还有未保存的修改。关闭编辑器将丢弃这些修改。"),
        QStringLiteral("丢弃并关闭"), true);
}

void ConfigEditorView::keyPressEvent(QKeyEvent* event)
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

void ConfigEditorView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), pal().background);
}
