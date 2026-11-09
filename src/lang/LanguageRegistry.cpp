#include "lang/LanguageRegistry.h"

#include "lang/JavaScriptLanguageProvider.h"
#include "lang/YamlLanguageProvider.h"

#include <QFileInfo>

namespace nq::lang {

LanguageRegistry& LanguageRegistry::instance()
{
    static LanguageRegistry registry;
    return registry;
}

LanguageRegistry::LanguageRegistry()
{
    registerBuiltinPlugins();
}

void LanguageRegistry::registerBuiltinPlugins()
{
    registerPlugin({QStringLiteral("yaml"), QStringLiteral("YAML"),
                    {QStringLiteral("yaml"), QStringLiteral("yml")},
                    [] { return std::make_unique<YamlLanguageProvider>(); }});
    registerPlugin({QStringLiteral("javascript"), QStringLiteral("JavaScript"),
                    {QStringLiteral("js"), QStringLiteral("mjs"), QStringLiteral("cjs")},
                    [] { return std::make_unique<JavaScriptLanguageProvider>(); }});
}

void LanguageRegistry::registerPlugin(LanguagePlugin plugin)
{
    if (plugin.id.isEmpty() || !plugin.factory)
        return;
    if (m_instances.find(plugin.id) != m_instances.end())
        return;

    std::unique_ptr<LanguageProvider> provider = plugin.factory();
    if (!provider)
        return;

    for (const QString& extension : plugin.extensions) {
        const QString normalized = extension.trimmed().toLower();
        if (!normalized.isEmpty())
            m_extensionToId.insert(normalized, plugin.id);
    }
    m_instances.emplace(plugin.id, std::move(provider));
    m_plugins.insert(plugin.id, std::move(plugin));
}

LanguageProvider* LanguageRegistry::providerById(const QString& id) const
{
    const auto found = m_instances.find(id);
    return found == m_instances.end() ? nullptr : found->second.get();
}

LanguageProvider* LanguageRegistry::providerForPath(const QString& path) const
{
    const QString extension = QFileInfo(path).suffix().toLower();
    const auto found = m_extensionToId.constFind(extension);
    if (found != m_extensionToId.cend()) {
        if (LanguageProvider* provider = providerById(found.value()))
            return provider;
    }
    // Configuration profiles are always YAML even if a profile file has no
    // conventional suffix.
    return providerById(QStringLiteral("yaml"));
}

QList<LanguageProvider*> LanguageRegistry::providers() const
{
    QList<LanguageProvider*> result;
    for (const auto& entry : m_instances)
        result.append(entry.second.get());
    return result;
}

QStringList LanguageRegistry::pluginIds() const
{
    return m_plugins.keys();
}

}  // namespace nq::lang
