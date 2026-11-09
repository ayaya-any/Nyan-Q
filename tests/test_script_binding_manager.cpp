#include "scripts/ScriptBindingManager.h"

#include <QTest>

using namespace nq::scripts;

namespace {
ScriptMeta script(const QString& id, const QString& name, int order, bool enabled = true)
{
    ScriptMeta meta;
    meta.id = id;
    meta.name = name;
    meta.order = order;
    meta.enabled = enabled;
    return meta;
}
}  // namespace

class ScriptBindingManagerTest : public QObject {
    Q_OBJECT

private slots:
    void globalAndProfileResolutionOrder();
    void excludesOneProfileWithoutAffectingOthers();
    void disablingGlobalKeepsProfileInclusion();
    void profileOverridesAndRemoval();
    void disabledScriptsAreIgnored();
};

void ScriptBindingManagerTest::globalAndProfileResolutionOrder()
{
    ScriptBindingManager bindings;
    bindings.setGlobal(QStringLiteral("global-b"), true);
    bindings.setGlobal(QStringLiteral("global-a"), true);
    bindings.setProfile(QStringLiteral("profile-2"), QStringLiteral("p1"), true);

    QVector<ScriptMeta> scripts = {
        script(QStringLiteral("global-b"), QStringLiteral("B"), 20),
        script(QStringLiteral("profile-2"), QStringLiteral("P"), 5),
        script(QStringLiteral("global-a"), QStringLiteral("A"), 10),
    };
    const QVector<ResolvedScript> resolved = bindings.resolve(scripts, QStringLiteral("p1"));
    QCOMPARE(resolved.size(), 3);
    QCOMPARE(resolved.at(0).script.id, QStringLiteral("global-a"));
    QCOMPARE(resolved.at(0).scope, ScriptScope::Global);
    QCOMPARE(resolved.at(1).script.id, QStringLiteral("global-b"));
    QCOMPARE(resolved.at(2).script.id, QStringLiteral("profile-2"));
    QCOMPARE(resolved.at(2).scope, ScriptScope::Profile);

    const QVector<ResolvedScript> other = bindings.resolve(scripts, QStringLiteral("p2"));
    QCOMPARE(other.size(), 2);
    QVERIFY(other.at(0).script.id != QStringLiteral("profile-2"));
}

void ScriptBindingManagerTest::excludesOneProfileWithoutAffectingOthers()
{
    ScriptBindingManager bindings;
    bindings.setGlobal(QStringLiteral("s1"), true);
    bindings.setGlobal(QStringLiteral("s2"), true);
    bindings.setProfile(QStringLiteral("s1"), QStringLiteral("p1"), false);

    QVERIFY(bindings.isEffective(QStringLiteral("s1"), QStringLiteral("p1")) == false);
    QVERIFY(bindings.isEffective(QStringLiteral("s1"), QStringLiteral("p2")) == true);
    QVERIFY(bindings.isEffective(QStringLiteral("s2"), QStringLiteral("p1")) == true);
    QVERIFY(bindings.isProfileExcluded(QStringLiteral("s1"), QStringLiteral("p1")));
    QVERIFY(!bindings.isProfileExcluded(QStringLiteral("s1"), QStringLiteral("p2")));

    // Re-enabling the profile removes the exclusion and therefore restores the
    // global association without a redundant record.
    bindings.setProfile(QStringLiteral("s1"), QStringLiteral("p1"), true);
    QVERIFY(bindings.isEffective(QStringLiteral("s1"), QStringLiteral("p1")));
    QCOMPARE(bindings.bindings().size(), 2);
}

void ScriptBindingManagerTest::disablingGlobalKeepsProfileInclusion()
{
    ScriptBindingManager bindings;
    bindings.setGlobal(QStringLiteral("s1"), true);
    bindings.setProfile(QStringLiteral("s1"), QStringLiteral("p1"), false);
    QVERIFY(bindings.isProfileExcluded(QStringLiteral("s1"), QStringLiteral("p1")));

    // Turning the global association off removes exclusions but leaves the
    // script available for a profile-specific association.
    bindings.setGlobal(QStringLiteral("s1"), false);
    QVERIFY(!bindings.isGlobalEnabled(QStringLiteral("s1")));
    QVERIFY(!bindings.isEffective(QStringLiteral("s1"), QStringLiteral("p1")));
    QCOMPARE(bindings.profilesForScript(QStringLiteral("s1")).size(), 0);

    bindings.setProfile(QStringLiteral("s1"), QStringLiteral("p1"), true);
    QVERIFY(bindings.isEffective(QStringLiteral("s1"), QStringLiteral("p1")));
    QVERIFY(!bindings.isEffective(QStringLiteral("s1"), QStringLiteral("p2")));
    QCOMPARE(bindings.profilesForScript(QStringLiteral("s1")).size(), 1);
}

void ScriptBindingManagerTest::profileOverridesAndRemoval()
{
    ScriptBindingManager bindings;
    bindings.setProfile(QStringLiteral("s1"), QStringLiteral("p1"), true);
    bindings.setProfile(QStringLiteral("s1"), QStringLiteral("p2"), true);
    bindings.removeProfile(QStringLiteral("p1"));
    QVERIFY(!bindings.isEffective(QStringLiteral("s1"), QStringLiteral("p1")));
    QVERIFY(bindings.isEffective(QStringLiteral("s1"), QStringLiteral("p2")));

    bindings.removeScript(QStringLiteral("s1"));
    QVERIFY(!bindings.isEffective(QStringLiteral("s1"), QStringLiteral("p2")));
    QVERIFY(bindings.bindings().isEmpty());
}

void ScriptBindingManagerTest::disabledScriptsAreIgnored()
{
    ScriptBindingManager bindings;
    bindings.setGlobal(QStringLiteral("s1"), true);
    QVector<ScriptMeta> scripts = {
        script(QStringLiteral("s1"), QStringLiteral("disabled"), 1, false)};
    QVERIFY(bindings.resolve(scripts, QStringLiteral("p1")).isEmpty());
}

QTEST_GUILESS_MAIN(ScriptBindingManagerTest)

#include "test_script_binding_manager.moc"
