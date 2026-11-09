#pragma once

#include "lang/LanguageProvider.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <memory>

namespace nq::lang {

struct LanguagePlugin {
    QString id;
    QString displayName;
    QStringList extensions;
    std::function<std::unique_ptr<LanguageProvider>()> factory;
};

// Compile-time plugin registry. Providers are created once and are stateless
// with respect to editor instances, so they can be shared by all editors.
class LanguageRegistry {
public:
    static LanguageRegistry& instance();

    void registerPlugin(LanguagePlugin plugin);

    LanguageProvider* providerById(const QString& id) const;
    LanguageProvider* providerForPath(const QString& path) const;
    QList<LanguageProvider*> providers() const;
    QStringList pluginIds() const;

private:
    LanguageRegistry();
    void registerBuiltinPlugins();

    QHash<QString, LanguagePlugin> m_plugins;
    std::map<QString, std::unique_ptr<LanguageProvider>> m_instances;
    QHash<QString, QString> m_extensionToId;
};

}  // namespace nq::lang
