#include "scripts/ScriptBindingManager.h"

#include <algorithm>

namespace nq::scripts {
namespace {
const QString kGlobalProfile = QStringLiteral("*");
}

void ScriptBindingManager::setBindings(QVector<ScriptBinding> bindings)
{
    m_bindings.clear();
    for (const ScriptBinding& binding : bindings) {
        if (binding.scriptId.isEmpty() || binding.profileId.isEmpty())
            continue;
        if (indexOf(binding.scriptId, binding.profileId) >= 0)
            continue;
        m_bindings.append(binding);
    }
}

int ScriptBindingManager::indexOf(const QString& scriptId, const QString& profileId) const
{
    for (int i = 0; i < m_bindings.size(); ++i) {
        if (m_bindings.at(i).scriptId == scriptId &&
            m_bindings.at(i).profileId == profileId) {
            return i;
        }
    }
    return -1;
}

void ScriptBindingManager::upsert(const ScriptBinding& binding)
{
    const int index = indexOf(binding.scriptId, binding.profileId);
    if (index >= 0) {
        m_bindings[index] = binding;
        return;
    }
    m_bindings.append(binding);
}

bool ScriptBindingManager::hasGlobalBinding(const QString& scriptId) const
{
    return indexOf(scriptId, kGlobalProfile) >= 0;
}

bool ScriptBindingManager::isGlobalEnabled(const QString& scriptId) const
{
    const int index = indexOf(scriptId, kGlobalProfile);
    return index >= 0 && m_bindings.at(index).enabled;
}

bool ScriptBindingManager::isProfileForcedOn(const QString& scriptId,
                                             const QString& profileId) const
{
    const int index = indexOf(scriptId, profileId);
    return index >= 0 && m_bindings.at(index).enabled;
}

bool ScriptBindingManager::isProfileExcluded(const QString& scriptId,
                                             const QString& profileId) const
{
    if (!isGlobalEnabled(scriptId))
        return false;
    const int index = indexOf(scriptId, profileId);
    return index >= 0 && !m_bindings.at(index).enabled;
}

bool ScriptBindingManager::isEffective(const QString& scriptId, const QString& profileId) const
{
    if (isGlobalEnabled(scriptId))
        return !isProfileExcluded(scriptId, profileId);
    return isProfileForcedOn(scriptId, profileId);
}

void ScriptBindingManager::setGlobal(const QString& scriptId, bool enabled)
{
    if (scriptId.isEmpty())
        return;

    const int globalIndex = indexOf(scriptId, kGlobalProfile);
    if (globalIndex >= 0)
        m_bindings[globalIndex].enabled = enabled;
    else if (enabled)
        m_bindings.append({scriptId, kGlobalProfile, true});

    if (enabled) {
        // Per-profile inclusions are redundant once the script is global. The
        // disabled records are exclusions and must stay.
        for (int i = m_bindings.size() - 1; i >= 0; --i) {
            if (m_bindings.at(i).scriptId == scriptId &&
                m_bindings.at(i).profileId != kGlobalProfile &&
                m_bindings.at(i).enabled) {
                m_bindings.removeAt(i);
            }
        }
    } else {
        // Exclusions only mean something while the global binding is on.
        for (int i = m_bindings.size() - 1; i >= 0; --i) {
            if (m_bindings.at(i).scriptId == scriptId &&
                m_bindings.at(i).profileId != kGlobalProfile &&
                !m_bindings.at(i).enabled) {
                m_bindings.removeAt(i);
            }
        }
    }
}

void ScriptBindingManager::setProfile(const QString& scriptId, const QString& profileId,
                                      bool enabled)
{
    if (scriptId.isEmpty() || profileId.isEmpty() || profileId == kGlobalProfile)
        return;

    const bool global = isGlobalEnabled(scriptId);
    const int index = indexOf(scriptId, profileId);
    if (global) {
        if (enabled) {
            if (index >= 0)
                m_bindings.removeAt(index);
            return;
        }
        upsert({scriptId, profileId, false});
        return;
    }

    if (enabled) {
        upsert({scriptId, profileId, true});
    } else if (index >= 0) {
        m_bindings.removeAt(index);
    }
}

void ScriptBindingManager::removeScript(const QString& scriptId)
{
    m_bindings.erase(std::remove_if(m_bindings.begin(), m_bindings.end(),
                                    [&scriptId](const ScriptBinding& binding) {
                                        return binding.scriptId == scriptId;
                                    }),
                     m_bindings.end());
}

void ScriptBindingManager::removeProfile(const QString& profileId)
{
    m_bindings.erase(std::remove_if(m_bindings.begin(), m_bindings.end(),
                                    [&profileId](const ScriptBinding& binding) {
                                        return binding.profileId == profileId;
                                    }),
                     m_bindings.end());
}

QVector<ResolvedScript> ScriptBindingManager::resolve(const QVector<ScriptMeta>& scripts,
                                                      const QString& profileId) const
{
    QVector<ResolvedScript> globalScripts;
    QVector<ResolvedScript> profileScripts;

    for (const ScriptMeta& script : scripts) {
        if (!script.enabled)
            continue;
        if (isGlobalEnabled(script.id)) {
            if (!isProfileExcluded(script.id, profileId))
                globalScripts.append({script, ScriptScope::Global});
        } else if (isProfileForcedOn(script.id, profileId)) {
            profileScripts.append({script, ScriptScope::Profile});
        }
    }

    const auto less = [](const ResolvedScript& a, const ResolvedScript& b) {
        if (a.script.order != b.script.order)
            return a.script.order < b.script.order;
        const int comparison = QString::localeAwareCompare(a.script.name, b.script.name);
        if (comparison != 0)
            return comparison < 0;
        return a.script.id < b.script.id;
    };
    std::stable_sort(globalScripts.begin(), globalScripts.end(), less);
    std::stable_sort(profileScripts.begin(), profileScripts.end(), less);

    QVector<ResolvedScript> result = globalScripts;
    result += profileScripts;
    return result;
}

QStringList ScriptBindingManager::profilesForScript(const QString& scriptId) const
{
    QStringList profiles;
    for (const ScriptBinding& binding : m_bindings) {
        if (binding.scriptId == scriptId && binding.profileId != kGlobalProfile)
            profiles.append(binding.profileId);
    }
    profiles.removeDuplicates();
    return profiles;
}

bool ScriptBindingManager::isUsedByAnyProfile(const QString& scriptId) const
{
    if (isGlobalEnabled(scriptId))
        return true;
    for (const ScriptBinding& binding : m_bindings) {
        if (binding.scriptId == scriptId && binding.enabled &&
            binding.profileId != kGlobalProfile)
            return true;
    }
    return false;
}

bool ScriptBindingManager::isUsedByProfile(const QString& scriptId,
                                           const QString& profileId) const
{
    return isEffective(scriptId, profileId);
}

}  // namespace nq::scripts
