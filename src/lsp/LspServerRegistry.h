#pragma once

#include "lsp/LspTypes.h"

#include <QObject>
#include <QVector>

class LspServerRegistry final : public QObject {
    Q_OBJECT

public:
    static LspServerRegistry& instance();

    QVector<LspServerProfile> profiles() const { return m_profiles; }
    LspServerProfile profileById(const QString& id) const;
    LspServerProfile profileForPath(const QString& path) const;
    LspServerProfile defaultProfile() const;
    QString configPath() const;

private:
    explicit LspServerRegistry(QObject* parent = nullptr);
    void loadConfiguredProfiles();
    void ensureDefaultProfile();

    QVector<LspServerProfile> m_profiles;
};
