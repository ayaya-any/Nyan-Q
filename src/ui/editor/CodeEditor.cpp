#include "ui/editor/CodeEditor.h"

#include "app/Fonts.h"
#include "app/Theme.h"
#include "lang/LanguageProvider.h"

#include <QAbstractItemView>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QListWidget>
#include <QPainter>
#include <QScrollBar>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFormat>
#include <QTimer>

#include <algorithm>
#include <utility>

class LineNumberArea final : public QWidget {
public:
    explicit LineNumberArea(CodeEditor* editor)
        : QWidget(editor)
        , m_editor(editor)
    {
    }

    QSize sizeHint() const override
    {
        return QSize(m_editor->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        m_editor->lineNumberAreaPaintEvent(event);
    }

private:
    CodeEditor* m_editor = nullptr;
};

CodeEditor::CodeEditor(QWidget* parent)
    : QPlainTextEdit(parent)
{
    setObjectName(QStringLiteral("codeEditor"));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFrameShape(QFrame::NoFrame);
    setFont(Fonts::mono(13));
    setCursorWidth(2);

    m_lineNumberArea = new LineNumberArea(this);
    m_diagnosticTimer = new QTimer(this);
    m_diagnosticTimer->setSingleShot(true);
    m_diagnosticTimer->setInterval(220);
    connect(m_diagnosticTimer, &QTimer::timeout, this, &CodeEditor::runDiagnostics);

    connect(this, &QPlainTextEdit::blockCountChanged, this,
            &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        highlightCurrentLine();
        emit cursorInfoChanged(currentLine(), currentColumn());
    });
    connect(this, &QPlainTextEdit::selectionChanged, this, &CodeEditor::highlightCurrentLine);
    connect(this, &QPlainTextEdit::textChanged, this, [this] {
        if (m_diagnosticTimer)
            m_diagnosticTimer->start();
    });
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        rebuildHighlighter();
        updateEditorPalette();
        m_lineNumberArea->update();
    });

    updateLineNumberAreaWidth();
    updateEditorPalette();
    highlightCurrentLine();
}

void CodeEditor::setLanguageProvider(const nq::lang::LanguageProvider* provider)
{
    if (m_provider == provider)
        return;
    m_provider = provider;
    rebuildHighlighter();
    runDiagnostics();
}

void CodeEditor::rebuildHighlighter()
{
    delete m_highlighter;
    m_highlighter = nullptr;
    if (!m_provider)
        return;

    const Palette& p = pal();
    nq::lang::LanguageTheme theme;
    theme.keyword = p.primary;
    theme.string = p.tertiary;
    theme.number = p.secondary;
    theme.comment = p.onSurfaceVariant;
    theme.punctuation = p.onSurfaceVariant;
    theme.function = p.secondary;
    theme.dark = p.dark;
    m_highlighter = m_provider->createHighlighter(document(), theme);
    if (m_highlighter)
        m_highlighter->rehighlight();
}

void CodeEditor::runDiagnostics()
{
    if (!m_provider) {
        setDiagnostics({});
        return;
    }
    const QString text = toPlainText();
    if (text.size() > 4 * 1024 * 1024) {
        nq::lang::Diagnostic diagnostic;
        diagnostic.severity = nq::lang::DiagnosticSeverity::Warning;
        diagnostic.message = QStringLiteral("文件超过 4 MB，已跳过实时语法检查");
        diagnostic.source = m_provider->displayName();
        setDiagnostics({diagnostic});
        return;
    }
    setDiagnostics(m_provider->diagnose(text, m_documentPath));
}

void CodeEditor::updateEditorPalette()
{
    const Palette& p = pal();
    QPalette editorPalette = palette();
    editorPalette.setColor(QPalette::Base, p.background);
    editorPalette.setColor(QPalette::Text, p.onSurface);
    editorPalette.setColor(QPalette::Highlight, p.primary);
    editorPalette.setColor(QPalette::HighlightedText, p.onPrimary);
    setPalette(editorPalette);

    setStyleSheet(QStringLiteral(
                      "QPlainTextEdit#codeEditor {"
                      " background-color: %1; color: %2; border: none;"
                      " border-radius: 0; padding: 8px 10px 8px 4px;"
                      " selection-background-color: %3; selection-color: %4; }")
                      .arg(p.background.name(), p.onSurface.name(), p.primary.name(),
                           p.onPrimary.name()));

    const qreal space = QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' '));
    setTabStopDistance(space * 2);
    if (m_highlighter)
        m_highlighter->rehighlight();
}

int CodeEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }
    return 18 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::updateLineNumberAreaWidth(int)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect& rect, int dy)
{
    if (dy)
        m_lineNumberArea->scroll(0, dy);
    else
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth();
}

void CodeEditor::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(
        QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event)
{
    const Palette& p = pal();
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), p.surfaceContainer);

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int current = textCursor().blockNumber();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setFont(font());
            painter.setPen(blockNumber == current ? p.primary : p.onSurfaceVariant);
            painter.drawText(0, top, m_lineNumberArea->width() - 7,
                             fontMetrics().height(), Qt::AlignRight,
                             QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

int CodeEditor::currentLine() const
{
    return textCursor().blockNumber() + 1;
}

int CodeEditor::currentColumn() const
{
    return textCursor().positionInBlock() + 1;
}

QTextCursor CodeEditor::cursorForDiagnostic(const nq::lang::Diagnostic& diagnostic) const
{
    if (!document())
        return {};
    QTextBlock block = document()->findBlockByNumber(qMax(0, diagnostic.line));
    if (!block.isValid())
        block = document()->lastBlock();
    QTextCursor cursor(block);
    cursor.movePosition(QTextCursor::StartOfBlock);
    const int offset = qBound(0, diagnostic.column, qMax(0, block.length() - 1));
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, offset);
    return cursor;
}

void CodeEditor::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> selections;
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(Theme::alpha(pal().surfaceContainerHigh, 170));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        selections.append(selection);
    }

    for (const nq::lang::Diagnostic& diagnostic : std::as_const(m_diagnostics)) {
        QTextCursor start = cursorForDiagnostic(diagnostic);
        if (start.isNull())
            continue;
        QTextCursor end = start;
        end.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor,
                         qMax(1, diagnostic.length));
        if (start.position() == end.position())
            continue;

        QTextEdit::ExtraSelection selection;
        const QColor color = diagnostic.severity == nq::lang::DiagnosticSeverity::Error
                                 ? pal().error
                                 : diagnostic.severity == nq::lang::DiagnosticSeverity::Warning
                                       ? pal().warning
                                       : pal().primary;
        selection.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        selection.format.setUnderlineColor(color);
        selection.cursor = start;
        selection.cursor.setPosition(end.position(), QTextCursor::KeepAnchor);
        selections.append(selection);
    }

    setExtraSelections(selections);
}

void CodeEditor::setDiagnostics(const QVector<nq::lang::Diagnostic>& diagnostics)
{
    m_diagnostics = diagnostics;
    int errors = 0;
    int warnings = 0;
    for (const nq::lang::Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == nq::lang::DiagnosticSeverity::Error)
            ++errors;
        else if (diagnostic.severity == nq::lang::DiagnosticSeverity::Warning)
            ++warnings;
    }
    highlightCurrentLine();
    emit diagnosticsChanged(errors, warnings);
}

void CodeEditor::requestCompletions()
{
    if (!m_provider || isReadOnly())
        return;
    nq::lang::CompletionRequest request;
    request.text = toPlainText();
    request.position = textCursor().position();
    request.filePath = m_documentPath;
    const QVector<nq::lang::CompletionItem> items = m_provider->completions(request);
    if (items.isEmpty()) {
        hideCompletions();
        return;
    }
    showCompletions(items);
}

void CodeEditor::showCompletions(const QVector<nq::lang::CompletionItem>& completions)
{
    if (completions.isEmpty() || isReadOnly()) {
        hideCompletions();
        return;
    }

    if (!m_completionPopup) {
        m_completionPopup = new QListWidget(this);
        m_completionPopup->setObjectName(QStringLiteral("completionPopup"));
        m_completionPopup->setFrameShape(QFrame::NoFrame);
        m_completionPopup->setFocusPolicy(Qt::NoFocus);
        m_completionPopup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_completionPopup->setUniformItemSizes(true);
        m_completionPopup->setAttribute(Qt::WA_StyledBackground, true);
        connect(m_completionPopup, &QListWidget::itemClicked, this,
                [this](QListWidgetItem*) { acceptCompletion(); });
    }

    const Palette& p = pal();
    m_completionPopup->setStyleSheet(
        QStringLiteral("QListWidget { background: %1; color: %2; border: 1px solid %3;"
                       " border-radius: 8px; padding: 4px; outline: none; }"
                       "QListWidget::item { padding: 5px 8px; border-radius: 5px; }"
                       "QListWidget::item:selected { background: %4; color: %5; }")
            .arg(p.surfaceContainerHigh.name(), p.onSurface.name(), p.outlineVariant.name(),
                 p.primaryContainer.name(), p.onPrimaryContainer.name()));

    m_completionPopup->clear();
    for (const nq::lang::CompletionItem& completion : completions) {
        QString text = completion.label;
        if (!completion.detail.isEmpty())
            text += QStringLiteral("    ") + completion.detail;
        auto* item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, completion.insertText);
        item->setData(Qt::UserRole + 1, completion.cursorOffset);
        item->setToolTip(completion.documentation.isEmpty() ? completion.detail
                                                            : completion.documentation);
        item->setSizeHint(QSize(360, 28));
        m_completionPopup->addItem(item);
    }
    m_completionPopup->setCurrentRow(0);

    const QRect cursor = cursorRect();
    const int width = 430;
    const int height = qMin(240, m_completionPopup->count() * 28 + 10);
    QPoint pos = viewport()->mapTo(this, cursor.bottomLeft());
    pos.rx() = qBound(4, pos.x(), qMax(4, this->width() - width - 4));
    if (pos.y() + height > this->height() - 4)
        pos.setY(qMax(4, pos.y() - height - cursor.height() - 2));
    m_completionPopup->setGeometry(pos.x(), pos.y(), width, height);
    m_completionPopup->show();
    m_completionPopup->raise();
}

void CodeEditor::hideCompletions()
{
    if (m_completionPopup)
        m_completionPopup->hide();
}

void CodeEditor::acceptCompletion()
{
    if (!m_completionPopup || !m_completionPopup->isVisible())
        return;
    QListWidgetItem* item = m_completionPopup->currentItem();
    if (!item)
        return;
    const QString insert = item->data(Qt::UserRole).toString();
    const int cursorOffset = item->data(Qt::UserRole + 1).toInt();

    QTextCursor cursor = textCursor();
    const int cursorPos = cursor.position();
    const int blockPosition = cursor.block().position();
    const QString line = cursor.block().text();
    int start = cursor.positionInBlock();
    while (start > 0) {
        const QChar ch = line.at(start - 1);
        if (!(ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-') ||
              ch == QLatin1Char('$')))
            break;
        --start;
    }
    cursor.setPosition(blockPosition + start);
    const int insertionStart = cursor.position();
    cursor.setPosition(cursorPos, QTextCursor::KeepAnchor);
    cursor.insertText(insert);
    if (cursorOffset >= 0) {
        cursor.setPosition(qMin(insertionStart + cursorOffset, document()->characterCount() - 1));
    }
    setTextCursor(cursor);
    hideCompletions();
    setFocus();
}

void CodeEditor::keyPressEvent(QKeyEvent* event)
{
    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    const bool alt = event->modifiers().testFlag(Qt::AltModifier);

    if (m_completionPopup && m_completionPopup->isVisible()) {
        if (event->key() == Qt::Key_Escape) {
            hideCompletions();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up ||
            event->key() == Qt::Key_PageDown || event->key() == Qt::Key_PageUp) {
            const int delta = event->key() == Qt::Key_Down || event->key() == Qt::Key_PageDown
                                  ? 1
                                  : -1;
            m_completionPopup->setCurrentRow(
                qBound(0, m_completionPopup->currentRow() + delta,
                       m_completionPopup->count() - 1));
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
            event->key() == Qt::Key_Tab) {
            acceptCompletion();
            event->accept();
            return;
        }
        if (event->key() != Qt::Key_Shift && event->key() != Qt::Key_Control &&
            event->key() != Qt::Key_Alt) {
            hideCompletions();
        }
    }

    if (ctrl && event->key() == Qt::Key_Space) {
        requestCompletions();
        event->accept();
        return;
    }
    if (ctrl && event->key() == Qt::Key_Slash) {
        toggleComment();
        event->accept();
        return;
    }
    if (ctrl && event->key() == Qt::Key_F) {
        emit searchRequested();
        event->accept();
        return;
    }
    if (ctrl && event->key() == Qt::Key_H) {
        emit replaceRequested();
        event->accept();
        return;
    }
    if (ctrl && shift && event->key() == Qt::Key_K) {
        deleteCurrentLine();
        event->accept();
        return;
    }
    if (alt && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
        if (shift)
            copyCurrentLine(event->key() == Qt::Key_Down ? 1 : -1);
        else
            moveCurrentLine(event->key() == Qt::Key_Down ? 1 : -1);
        event->accept();
        return;
    }
    if (ctrl && event->key() == Qt::Key_Return) {
        insertLine(!shift);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        indentSelection(event->key() == Qt::Key_Backtab || shift);
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !ctrl && !alt) {
        autoIndentNewline();
        event->accept();
        return;
    }

    QPlainTextEdit::keyPressEvent(event);

    if (!event->text().isEmpty()) {
        const QChar ch = event->text().at(0);
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-') ||
            ch == QLatin1Char('$')) {
            QTimer::singleShot(90, this, &CodeEditor::requestCompletions);
        }
    }
}

void CodeEditor::moveCurrentLine(int delta)
{
    const QTextBlock block = textCursor().block();
    const QTextBlock target = delta > 0 ? block.next() : block.previous();
    if (!block.isValid() || !target.isValid())
        return;
    const int column = textCursor().positionInBlock();

    QTextCursor first(block);
    first.movePosition(QTextCursor::StartOfBlock);
    first.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    QTextCursor second(target);
    second.movePosition(QTextCursor::StartOfBlock);
    second.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);

    const QString firstText = first.selectedText();
    const QString secondText = second.selectedText();
    QTextCursor edit(document());
    edit.beginEditBlock();
    second.insertText(firstText);
    first.insertText(secondText);
    edit.endEditBlock();

    QTextCursor result(delta > 0 ? block.next() : block.previous());
    result.movePosition(QTextCursor::StartOfBlock);
    result.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor,
                        qMin(column, result.block().length() - 1));
    setTextCursor(result);
}

void CodeEditor::copyCurrentLine(int delta)
{
    const QTextBlock block = textCursor().block();
    if (!block.isValid())
        return;
    const QString text = block.text();
    QTextCursor edit(document());
    edit.beginEditBlock();
    if (delta > 0) {
        QTextCursor end(block);
        end.movePosition(QTextCursor::EndOfBlock);
        end.insertText(QLatin1Char('\n') + text);
        setTextCursor(end);
    } else {
        QTextCursor start(block);
        start.movePosition(QTextCursor::StartOfBlock);
        start.insertText(text + QLatin1Char('\n'));
        start.movePosition(QTextCursor::StartOfBlock);
        start.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor,
                           qMin(text.size(), start.block().length() - 1));
        setTextCursor(start);
    }
    edit.endEditBlock();
}

void CodeEditor::deleteCurrentLine()
{
    QTextCursor cursor = textCursor();
    QTextBlock block = cursor.block();
    if (!block.isValid())
        return;
    cursor.beginEditBlock();
    cursor.setPosition(block.position());
    if (block.next().isValid())
        cursor.setPosition(block.next().position(), QTextCursor::KeepAnchor);
    else
        cursor.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.endEditBlock();
    setTextCursor(cursor);
}

void CodeEditor::insertLine(bool below)
{
    QTextBlock block = textCursor().block();
    if (!block.isValid())
        return;
    const QString indent =
        block.text().left(block.text().size() - block.text().trimmed().size());
    QTextCursor cursor(block);
    if (below)
        cursor.movePosition(QTextCursor::EndOfBlock);
    else
        cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.insertText(below ? QLatin1Char('\n') + indent : indent + QLatin1Char('\n'));
    if (!below)
        cursor.movePosition(QTextCursor::StartOfBlock);
    else
        cursor.movePosition(QTextCursor::EndOfBlock);
    setTextCursor(cursor);
}

void CodeEditor::toggleComment()
{
    const QString prefix = m_provider ? m_provider->lineCommentPrefix() : QStringLiteral("# ");
    const QString marker = prefix.trimmed();
    const int markerLength = qMax(1, marker.size());
    QTextCursor cursor = textCursor();
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    QTextBlock first = document()->findBlock(start);
    QTextBlock last = document()->findBlock(end);
    if (end == last.position() && last != first)
        last = last.previous();

    bool allCommented = true;
    bool anyContent = false;
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        const QString text = block.text();
        if (text.trimmed().isEmpty())
            continue;
        anyContent = true;
        const int indent = text.size() - text.trimmed().size();
        if (!text.mid(indent).startsWith(marker))
            allCommented = false;
        if (block == last)
            break;
    }
    if (!anyContent)
        return;

    QTextCursor edit(document());
    edit.beginEditBlock();
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        const QString text = block.text();
        const int indent = text.size() - text.trimmed().size();
        QTextCursor lineCursor(block);
        lineCursor.setPosition(block.position() + indent);
        if (allCommented) {
            lineCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, markerLength);
            lineCursor.removeSelectedText();
            if (lineCursor.block().text().mid(indent).startsWith(QLatin1Char(' ')))
                lineCursor.deleteChar();
        } else {
            lineCursor.insertText(prefix);
        }
        if (block == last)
            break;
    }
    edit.endEditBlock();
}

void CodeEditor::indentSelection(bool outdent)
{
    const QString spaces(QStringLiteral("  "));
    QTextCursor cursor = textCursor();
    if (!cursor.hasSelection() && !outdent) {
        const int col = cursor.positionInBlock();
        cursor.insertText(QString(2 - (col % 2), QLatin1Char(' ')));
        return;
    }

    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    QTextBlock first = document()->findBlock(start);
    QTextBlock last = document()->findBlock(end);
    if (end == last.position() && last != first)
        last = last.previous();

    cursor.beginEditBlock();
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        QTextCursor lineCursor(block);
        lineCursor.movePosition(QTextCursor::StartOfBlock);
        if (outdent) {
            int remove = 0;
            while (remove < 2 && block.text().mid(remove, 1) == QLatin1String(" "))
                ++remove;
            if (remove > 0) {
                lineCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, remove);
                lineCursor.removeSelectedText();
            }
        } else {
            lineCursor.insertText(spaces);
        }
        if (block == last)
            break;
    }
    cursor.endEditBlock();
}

void CodeEditor::autoIndentNewline()
{
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection())
        cursor.removeSelectedText();
    const QString line = cursor.block().text();
    QString indent = line.left(line.size() - line.trimmed().size());
    const QString trimmed = line.trimmed();
    if (trimmed.endsWith(QLatin1Char(':')) || trimmed.endsWith(QLatin1Char('{')) ||
        trimmed.endsWith(QLatin1Char('(')) || trimmed.endsWith(QLatin1Char('['))) {
        indent += QStringLiteral("  ");
    }
    cursor.insertText(QLatin1Char('\n') + indent);
    setTextCursor(cursor);
}
