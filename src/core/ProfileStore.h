#pragma once

#include "core/Types.h"

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <QVector>

#include <optional>

class QNetworkReply;

class ProfileStore final : public QObject {
    Q_OBJECT

public:
    explicit ProfileStore(QObject* parent = nullptr);
    ~ProfileStore() override;

    void initialize(const QString& profilesDir);

    QVector<cq::ProfileMeta> profiles() const;
    QString activeId() const;
    std::optional<cq::ProfileMeta> activeProfile() const;
    std::optional<cq::ProfileMeta> find(const QString& id) const;
    QString configPath(const cq::ProfileMeta& profile) const;
    QByteArray configContent(const QString& id, QString* error = nullptr) const;
    bool saveConfigContent(const QString& id, const QByteArray& content,
                           QString* error = nullptr);
    bool renameProfile(const QString& id, const QString& name,
                       QString* error = nullptr);
    bool isBusy() const { return m_busy; }
    QString directory() const { return m_directory; }

    void importUrl(const QString& name, const QUrl& url);
    void importFile(const QString& path, const QString& name = {});
    void importContent(const QString& name, const QByteArray& content,
                       const QString& type = QStringLiteral("local"), const QUrl& source = {});
    void refreshProfile(const QString& id);
    void removeProfile(const QString& id);
    void activate(const QString& id);
    void seedDemoProfiles();

signals:
    void changed();
    void activeChanged(const QString& id);
    void busyChanged(bool busy);
    void operationSucceeded(const QString& message);
    void operationFailed(const QString& message);
    void profileUpdated(const QString& id);

private:
    enum class PendingKind { Import, Refresh };

    struct Pending {
        PendingKind kind = PendingKind::Import;
        QString profileId;
        QString requestedName;
        QUrl url;
        QNetworkReply* reply = nullptr;
    };

    void loadMetadata();
    void saveMetadata() const;
    void ensureBuiltin();
    void ensureActive();
    void setBusy(bool busy);
    void beginDownload(PendingKind kind, const QString& profileId, const QString& name,
                       const QUrl& url);
    void finishDownload(QNetworkReply* reply);
    void fail(const QString& message);
    bool validateContent(const QByteArray& data, QString* error) const;
    bool writeProfileFile(const QString& fileName, const QByteArray& data, QString* error) const;
    QString makeId() const;
    QString uniqueName(const QString& desired) const;
    QString fileNameForId(const QString& id) const;
    static QString parseProfileTitle(const QByteArray& value);
    static void applyUserInfo(const QByteArray& value, cq::ProfileMeta& profile);
    static QByteArray userAgent();

    QString m_directory;
    QString m_activeId;
    QVector<cq::ProfileMeta> m_profiles;
    QNetworkAccessManager m_network;
    Pending m_pending;
    bool m_busy = false;
};
