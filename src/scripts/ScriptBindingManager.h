#pragma once

#include "scripts/ScriptTypes.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace nq::scripts {

struct ResolvedScript {
    ScriptMeta script;
    ScriptScope scope = ScriptScope::Global;
};

// Owns the script <-> subscription associations. Stable IDs are used for both
// sides; "*" is the global (all profiles) association.
class ScriptBindingManager {
public:
    ScriptBindingManager() = default;

    void setBindings(QVector<ScriptBinding> bindings);
    const QVector<ScriptBinding>& bindings() const { return m_bindings; }

    bool isGlobalEnabled(const QString& scriptId) const;
    bool hasGlobalBinding(const QString& scriptId) const;
    bool isProfileForcedOn(const QString& scriptId, const QString& profileId) const;
    bool isProfileExcluded(const QString& scriptId, const QString& profileId) const;
    bool isEffective(const QString& scriptId, const QString& profileId) const;

    // Enables/disables the script for all profiles. Existing per-profile
    // inclusions are kept; an inclusion that becomes redundant is removed.
    void setGlobal(const QString& scriptId, bool enabled);

    // Enables/disables the script for one profile. When the global binding is
    // enabled, disabling one profile records an exclusion without affecting
    // other profiles.
    void setProfile(const QString& scriptId, const QString& profileId, bool enabled);

    // Removes all associations for a script/profile owner.
    void removeScript(const QString& scriptId);
    void removeProfile(const QString& profileId);

    // Returns global scripts first, then profile-specific scripts. Each group
    // is ordered by the script's user-defined order and then by name.
    QVector<ResolvedScript> resolve(const QVector<ScriptMeta>& scripts,
                                    const QString& profileId) const;

    QStringList profilesForScript(const QString& scriptId) const;
    bool isUsedByAnyProfile(const QString& scriptId) const;
    bool isUsedByProfile(const QString& scriptId, const QString& profileId) const;

private:
    int indexOf(const QString& scriptId, const QString& profileId) const;
    void upsert(const ScriptBinding& binding);

    QVector<ScriptBinding> m_bindings;
};

}  // namespace nq::scripts
