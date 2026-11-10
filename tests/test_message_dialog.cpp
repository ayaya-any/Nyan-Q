#include "app/Fonts.h"
#include "ui/dialogs/MessageDialog.h"

#include <QTest>
#include <QWidget>

class MessageDialogTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { Fonts::load(); }
    void renders();
};

void MessageDialogTest::renders()
{
    QWidget host;
    host.resize(900, 600);
    host.show();

    MessageDialog dialog(&host, QStringLiteral("TUN 模式"),
                         QStringLiteral("已取消管理员授权，TUN 模式已关闭。"), false);
    dialog.show();
    QTest::qWait(250);

    QVERIFY(dialog.isVisible());
    const QPixmap shot = dialog.grab();
    QVERIFY(!shot.isNull());
    QVERIFY(shot.width() > 0 && shot.height() > 0);
}

QTEST_MAIN(MessageDialogTest)

#include "test_message_dialog.moc"
