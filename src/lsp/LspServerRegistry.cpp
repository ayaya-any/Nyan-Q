#include "lsp/LspServerRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>

namespace {

QString expandPlaceholders(QString value)
{
    const QString app = QCoreApplication::applicationDirPath();
    value.replace(QStringLiteral("${app}"), QDir::toNativeSeparators(app));
    return value;
}

QString findBundledServer()
{
    const QString app = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(app).filePath(QStringLiteral("lsp/yaml-language-server.exe")),
        QDir(app).filePath(QStringLiteral(
            "../../tools/yaml-language-server/yaml-language-server.exe")),
        QDir(app).filePath(QStringLiteral(
            "../../../tools/yaml-language-server/yaml-language-server.exe")),
        qEnvironmentVariable("NYANQ_YAML_LSP_SERVER"),
        qEnvironmentVariable("CLASHQ_YAML_LSP_SERVER"),
    };
    for (const QString& candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();
    }
    return {};
}

LspServerProfile yamlProfile()
{
    LspServerProfile profile;
    profile.id = QStringLiteral("yaml");
    profile.name = QStringLiteral("YAML Language Server");
    profile.languageId = QStringLiteral("yaml");
    profile.extensions = {QStringLiteral("yaml"), QStringLiteral("yml")};

    const QString server = findBundledServer();
    if (!server.isEmpty()) {
        profile.command = server;
    } else {
        const QString executable =
            QStandardPaths::findExecutable(QStringLiteral("yaml-language-server"));
        profile.command = executable.isEmpty() ? QStringLiteral("yaml-language-server")
                                               : executable;
    }
    profile.arguments = {QStringLiteral("--stdio")};

    QJsonObject yamlOptions{
        {QStringLiteral("validate"), true},
        {QStringLiteral("hover"), true},
        {QStringLiteral("completion"), true},
        {QStringLiteral("format"), QJsonObject{{QStringLiteral("enable"), true}}},
    };

    // A bundled schema makes schema-driven completion and diagnostics work
    // for Clash/Mihomo keys without embedding any language logic in Nyan-Q.
    const QString schema = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("lsp/clash-config.schema.json"));
    if (QFileInfo::exists(schema)) {
        const QString schemaUri = QUrl::fromLocalFile(schema).toString(QUrl::FullyEncoded);
        yamlOptions.insert(
            QStringLiteral("schemas"),
            QJsonObject{{schemaUri,
                         QJsonArray{QStringLiteral("*.yaml"), QStringLiteral("**/*.yaml"),
                                    QStringLiteral("*.yml"), QStringLiteral("**/*.yml")}}});
    }

    profile.initializationOptions =
        QJsonObject{{QStringLiteral("yaml"), yamlOptions}};
    return profile;
}

}  // namespace

LspServerRegistry& LspServerRegistry::instance()
{
    static LspServerRegistry registry;
    return registry;
}

LspServerRegistry::LspServerRegistry(QObject* parent)
    : QObject(parent)
{
    ensureDefaultProfile();
    loadConfiguredProfiles();
    if (m_profiles.isEmpty())
        m_profiles.push_back(yamlProfile());
}

QString LspServerRegistry::configPath() const
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("lsp-servers.json"));
}

void LspServerRegistry::ensureDefaultProfile()
{
    m_profiles.push_back(yamlProfile());
}

void LspServerRegistry::loadConfiguredProfiles()
{
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonArray servers =
        document.object().value(QStringLiteral("servers")).toArray();
    for (const QJsonValue& value : servers) {
        const QJsonObject object = value.toObject();
        const QString id = object.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;

        LspServerProfile profile;
        profile.id = id;
        profile.name = object.value(QStringLiteral("name")).toString(id);
        profile.languageId =
            object.value(QStringLiteral("languageId")).toString(QStringLiteral("yaml"));
        for (const QJsonValue& extension :
             object.value(QStringLiteral("extensions")).toArray()) {
            profile.extensions.push_back(extension.toString());
        }
        profile.command =
            expandPlaceholders(object.value(QStringLiteral("command")).toString());
        for (const QJsonValue& argument :
             object.value(QStringLiteral("args")).toArray()) {
            profile.arguments.push_back(expandPlaceholders(argument.toString()));
        }
        profile.initializationOptions =
            object.value(QStringLiteral("initializationOptions")).toObject();

        for (int i = m_profiles.size() - 1; i >= 0; --i) {
            if (m_profiles.at(i).id.compare(id, Qt::CaseInsensitive) == 0)
                m_profiles.removeAt(i);
        }
        m_profiles.push_back(profile);
    }
}

LspServerProfile LspServerRegistry::defaultProfile() const
{
    return m_profiles.isEmpty() ? yamlProfile() : m_profiles.first();
}

LspServerProfile LspServerRegistry::profileById(const QString& id) const
{
    for (const LspServerProfile& profile : m_profiles) {
        if (profile.id.compare(id, Qt::CaseInsensitive) == 0)
            return profile;
    }
    return defaultProfile();
}

LspServerProfile LspServerRegistry::profileForPath(const QString& path) const
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    for (const LspServerProfile& profile : m_profiles) {
        if (profile.extensions.contains(suffix, Qt::CaseInsensitive))
            return profile;
    }
    return defaultProfile();
}
