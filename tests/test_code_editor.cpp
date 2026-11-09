#include "lang/LanguageRegistry.h"
#include "ui/editor/CodeEditor.h"

#include <QListWidget>
#include <QTest>
#include <QTextCursor>

using namespace nq::lang;

class CodeEditorTest : public QObject {
    Q_OBJECT

private slots:
    void yamlCompletionsUseThePopup();
    void javascriptCompletionsReplaceOnlyThePrefix();
    void diagnosticsAreAppliedToTheDocument();
};

void CodeEditorTest::yamlCompletionsUseThePopup()
{
    CodeEditor editor;
    editor.resize(900, 600);
    editor.setLanguageProvider(
        LanguageRegistry::instance().providerById(QStringLiteral("yaml")));
    editor.setPlainText(QStringLiteral("mix"));
    editor.moveCursor(QTextCursor::End);
    editor.show();
    editor.setFocus();
    QTest::qWait(30);

    QTest::keyClick(&editor, Qt::Key_Space, Qt::ControlModifier);
    QCoreApplication::processEvents();

    auto* popup = editor.findChild<QListWidget*>(QStringLiteral("completionPopup"));
    QVERIFY(popup);
    QVERIFY(popup->isVisible());
    QVERIFY(popup->count() >= 1);
    QListWidgetItem* item = popup->item(0);
    QCOMPARE(item->data(Qt::UserRole).toString(), QStringLiteral("mixed-port: "));

    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.toPlainText(), QStringLiteral("mixed-port: "));
}

void CodeEditorTest::javascriptCompletionsReplaceOnlyThePrefix()
{
    CodeEditor editor;
    editor.resize(900, 600);
    editor.setLanguageProvider(
        LanguageRegistry::instance().providerById(QStringLiteral("javascript")));
    editor.setPlainText(QStringLiteral("const value = config.pro"));
    editor.moveCursor(QTextCursor::End);
    editor.show();
    editor.setFocus();
    QTest::qWait(30);

    QTest::keyClick(&editor, Qt::Key_Space, Qt::ControlModifier);
    QCoreApplication::processEvents();
    auto* popup = editor.findChild<QListWidget*>(QStringLiteral("completionPopup"));
    QVERIFY(popup && popup->isVisible());
    QVERIFY(popup->count() >= 1);
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.toPlainText(), QStringLiteral("const value = config.proxies"));
}

void CodeEditorTest::diagnosticsAreAppliedToTheDocument()
{
    CodeEditor editor;
    editor.setLanguageProvider(
        LanguageRegistry::instance().providerById(QStringLiteral("yaml")));
    editor.setPlainText(QStringLiteral("proxies:\n  - name: [unclosed\n"));
    QTRY_VERIFY_WITH_TIMEOUT(!editor.diagnostics().isEmpty(), 1000);
    QCOMPARE(editor.diagnostics().first().severity, DiagnosticSeverity::Error);
}

QTEST_MAIN(CodeEditorTest)

#include "test_code_editor.moc"
