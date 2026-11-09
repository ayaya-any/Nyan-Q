#pragma once

#include "scripts/ScriptTypes.h"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace nq::scripts {

class ScriptRepository {
public:
    struct Catalog {
        QVector<ScriptMeta> scripts;
        QVector<ScriptBinding> bindings;
        ScriptSettings settings;
    };

    ScriptRepository() = default;
    explicit ScriptRepository(QString rootDirectory);

    bool initialize(QString* error = nullptr);
    bool reload(QString* error = nullptr);

    const Catalog& catalog() const { return m_catalog; }
    Catalog& catalog() { return m_catalog; }
    const ScriptSettings& settings() const { return m_catalog.settings; }
    ScriptSettings& settings() { return m_catalog.settings; }

    ScriptMeta* find(const QString& id);
    const ScriptMeta* find(const QString& id) const;

    QString rootDirectory() const { return m_root; }
    QString metadataPath() const;
    QString contentPath(const ScriptMeta& script) const;
    QString versionPath(const ScriptMeta& script, const ScriptVersion& version) const;

    bool addScript(ScriptMeta meta, const QByteArray& content, QString* error = nullptr);
    bool updateContent(const QString& id, const QByteArray& content, const QString& note,
                       bool* changed = nullptr, QString* error = nullptr);
    bool removeScript(const QString& id, QString* error = nullptr);
    bool save(QString* error = nullptr);

    QByteArray content(const QString& id, QString* error = nullptr) const;
    QByteArray versionContent(const QString& id, const QString& versionId,
                              QString* error = nullptr) const;

    // Restores a previous version by saving its bytes as a new current version.
    bool restoreVersion(const QString& id, const QString& versionId, QString* error = nullptr);

    static int maxVersions();
    static qint64 maxScriptBytes();

private:
    bool load(QString* error);
    bool writeCatalog(const Catalog& catalog, QString* error) const;
    ScriptVersion createVersion(const ScriptMeta& script, const QByteArray& content,
                                const QString& note) const;
    QString versionFileFor(const QString& scriptId, const QString& versionId) const;
    void trimVersions(ScriptMeta* script);
    static bool validId(const QString& value);
    static bool validVersionId(const QString& value);
    static bool isSafeRelative(const QString& value);

    QString m_root;
    Catalog m_catalog;
    bool m_loaded = false;
};

}  // namespace nq::scripts
