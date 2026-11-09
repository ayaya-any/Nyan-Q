#include "scripts/ScriptRepository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace nq::scripts;

class ScriptRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void addsAndReloadsScript();
    void updatesCreateVersionsAndRollback();
    void unchangedContentDoesNotCreateVersion();
    void retainsBoundedHistory();
    void removesScriptAndBindings();
    void refusesCorruptMetadata();
    void ignoresUnsafeStoredPaths();
};

namespace {
ScriptMeta makeMeta(const QString& name)
{
    ScriptMeta meta;
    meta.name = name;
    meta.kind = ScriptSourceKind::Local;
    return meta;
}
}  // namespace

void ScriptRepositoryTest::addsAndReloadsScript()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ScriptRepository repository(dir.filePath(QStringLiteral("scripts")));
    QString error;
    QVERIFY2(repository.initialize(&error), qPrintable(error));

    const QByteArray source = "function main(config) { config.mode = 'global'; return config; }\n";
    QVERIFY2(repository.addScript(makeMeta(QStringLiteral("测试脚本")), source, &error),
             qPrintable(error));
    QCOMPARE(repository.catalog().scripts.size(), 1);
    const ScriptMeta script = repository.catalog().scripts.first();
    QVERIFY(!script.id.isEmpty());
    QVERIFY(script.clientSha256.size() == 64);
    QVERIFY(QFileInfo::exists(repository.contentPath(script)));
    QCOMPARE(script.versions.size(), 1);
    QVERIFY(script.versions.first().current);
    QCOMPARE(repository.content(script.id), source);

    ScriptRepository reloaded(dir.filePath(QStringLiteral("scripts")));
    QVERIFY2(reloaded.initialize(&error), qPrintable(error));
    QCOMPARE(reloaded.catalog().scripts.size(), 1);
    QCOMPARE(reloaded.catalog().scripts.first().name, QStringLiteral("测试脚本"));
    QCOMPARE(reloaded.catalog().scripts.first().clientSha256, script.clientSha256);
    QCOMPARE(reloaded.content(script.id), source);

    // Bindings are serialized by the repository even though the manager owns
    // their semantics.
    reloaded.catalog().bindings.append({script.id, QStringLiteral("*"), true});
    reloaded.catalog().bindings.append({script.id, QStringLiteral("profile-1"), false});
    QVERIFY(reloaded.save(&error));
    ScriptRepository third(dir.filePath(QStringLiteral("scripts")));
    QVERIFY2(third.initialize(&error), qPrintable(error));
    QCOMPARE(third.catalog().bindings.size(), 2);
}

void ScriptRepositoryTest::updatesCreateVersionsAndRollback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ScriptRepository repository(dir.filePath(QStringLiteral("scripts")));
    QString error;
    QVERIFY(repository.initialize(&error));
    const QByteArray v1 = "function main(config) { return config; } // v1\n";
    const QByteArray v2 = "function main(config) { return config; } // v2\n";
    QVERIFY2(repository.addScript(makeMeta(QStringLiteral("版本脚本")), v1, &error),
             qPrintable(error));
    const QString id = repository.catalog().scripts.first().id;
    const QString v1Id = repository.catalog().scripts.first().versions.first().id;

    bool changed = false;
    QVERIFY2(repository.updateContent(id, v2, QStringLiteral("test v2"), &changed, &error),
             qPrintable(error));
    QVERIFY(changed);
    QCOMPARE(repository.content(id), v2);
    const ScriptMeta* script = repository.find(id);
    QVERIFY(script);
    QCOMPARE(script->versions.size(), 2);
    QCOMPARE(script->versions.first().note, QStringLiteral("test v2"));
    QVERIFY(script->versions.first().current);
    QVERIFY(!script->versions.at(1).current);

    QVERIFY2(repository.restoreVersion(id, v1Id, &error), qPrintable(error));
    QCOMPARE(repository.content(id), v1);
    script = repository.find(id);
    QVERIFY(script);
    QCOMPARE(script->versions.size(), 3);
    QCOMPARE(script->versions.first().note.left(2), QStringLiteral("回滚"));
}

void ScriptRepositoryTest::unchangedContentDoesNotCreateVersion()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ScriptRepository repository(dir.filePath(QStringLiteral("scripts")));
    QString error;
    QVERIFY(repository.initialize(&error));
    const QByteArray source = "function main(config) { return config; }\n";
    QVERIFY(repository.addScript(makeMeta(QStringLiteral("不变")), source, &error));
    const QString id = repository.catalog().scripts.first().id;
    bool changed = true;
    QVERIFY(repository.updateContent(id, source, QStringLiteral("same"), &changed, &error));
    QVERIFY(!changed);
    QCOMPARE(repository.find(id)->versions.size(), 1);
}

void ScriptRepositoryTest::retainsBoundedHistory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ScriptRepository repository(dir.filePath(QStringLiteral("scripts")));
    QString error;
    QVERIFY(repository.initialize(&error));
    QVERIFY(repository.addScript(makeMeta(QStringLiteral("历史")), QByteArray("function main(c){return c;}\n"), &error));
    const QString id = repository.catalog().scripts.first().id;

    for (int i = 0; i < 15; ++i) {
        const QByteArray content =
            QStringLiteral("function main(c){ return c; } // %1\n").arg(i).toUtf8();
        QVERIFY2(repository.updateContent(id, content, QStringLiteral("v%1").arg(i), nullptr,
                                         &error),
                 qPrintable(error));
    }
    const ScriptMeta* script = repository.find(id);
    QVERIFY(script);
    QCOMPARE(script->versions.size(), ScriptRepository::maxVersions());
    int files = 0;
    const QDir versionsDir(QDir(repository.rootDirectory())
                               .filePath(QStringLiteral("versions/%1").arg(id)));
    files = versionsDir.entryList({QStringLiteral("*.js")}, QDir::Files).size();
    QCOMPARE(files, ScriptRepository::maxVersions());
}

void ScriptRepositoryTest::removesScriptAndBindings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ScriptRepository repository(dir.filePath(QStringLiteral("scripts")));
    QString error;
    QVERIFY(repository.initialize(&error));
    QVERIFY(repository.addScript(makeMeta(QStringLiteral("删除")), QByteArray("function main(c){return c;}\n"), &error));
    const ScriptMeta script = repository.catalog().scripts.first();
    repository.catalog().bindings.append({script.id, QStringLiteral("*"), true});
    QVERIFY(repository.save(&error));

    const QString contentPath = repository.contentPath(script);
    QVERIFY2(repository.removeScript(script.id, &error), qPrintable(error));
    QVERIFY(!repository.find(script.id));
    QVERIFY(repository.catalog().bindings.isEmpty());
    QVERIFY(!QFileInfo::exists(contentPath));
    QVERIFY(!QDir(QDir(repository.rootDirectory())
                      .filePath(QStringLiteral("versions/%1").arg(script.id)))
                 .exists());
    QCOMPARE(repository.catalog().scripts.size(), 0);
}

void ScriptRepositoryTest::refusesCorruptMetadata()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString scriptsDir = dir.filePath(QStringLiteral("scripts"));
    QDir().mkpath(scriptsDir);
    QFile file(QDir(scriptsDir).filePath(QStringLiteral("scripts.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ this is not json");
    file.close();

    ScriptRepository repository(scriptsDir);
    QString error;
    QVERIFY(!repository.initialize(&error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(repository.catalog().scripts.size(), 0);
}

void ScriptRepositoryTest::ignoresUnsafeStoredPaths()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString scriptsDir = dir.filePath(QStringLiteral("scripts"));
    QDir().mkpath(scriptsDir);
    QJsonObject script;
    script.insert(QStringLiteral("id"), QStringLiteral("abc123"));
    script.insert(QStringLiteral("name"), QStringLiteral("unsafe"));
    script.insert(QStringLiteral("fileName"), QStringLiteral("../../outside.js"));
    script.insert(QStringLiteral("kind"), QStringLiteral("local"));
    script.insert(QStringLiteral("clientSha256"),
                  QString::fromLatin1(sha256Hex(QByteArray("x"))));
    QJsonObject root;
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("scripts"), QJsonArray{script});
    QFile file(QDir(scriptsDir).filePath(QStringLiteral("scripts.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(root).toJson());
    file.close();

    ScriptRepository repository(scriptsDir);
    QString error;
    QVERIFY2(repository.initialize(&error), qPrintable(error));
    QCOMPARE(repository.catalog().scripts.size(), 1);
    // The repository regenerates the on-disk name and never escapes its root.
    QCOMPARE(repository.catalog().scripts.first().fileName,
             QStringLiteral("script-abc123.js"));
    QCOMPARE(repository.catalog().scripts.first().lastError,
             QStringLiteral("脚本文件缺失"));
}

QTEST_GUILESS_MAIN(ScriptRepositoryTest)

#include "test_script_repository.moc"
