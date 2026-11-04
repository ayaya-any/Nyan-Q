#pragma once

#include "lsp/LspTypes.h"

#include <QHash>
#include <QObject>

class QProcess;
class QTimer;

class LspClient final : public QObject {
    Q_OBJECT

public:
    enum class State { Stopped, Starting, Ready, Failed };
    Q_ENUM(State)

    explicit LspClient(QObject* parent = nullptr);
    ~LspClient() override;

    void start(const LspServerProfile& profile, const QString& rootPath);
    void stop();

    void openDocument(const QString& path, const QString& text);
    void changeDocument(const QString& path, const QString& text);
    void saveDocument(const QString& path);
    void closeDocument(const QString& path);

    int requestCompletion(const QString& path, int line, int character);
    int requestFormatting(const QString& path);

    State state() const { return m_state; }
    bool isReady() const { return m_state == State::Ready; }
    LspServerProfile profile() const { return m_profile; }

signals:
    void stateChanged(LspClient::State state);
    void ready();
    void completionReady(int requestId, const QVector<LspCompletion>& items);
    void formattingReady(int requestId, const QVector<LspTextEdit>& edits);
    void diagnosticsReady(const QString& path, const QVector<LspDiagnostic>& diagnostics);
    void logMessage(const QString& message);
    void errorMessage(const QString& message);

private:
    void sendMessage(const QJsonObject& message);
    void sendNotification(const QString& method, const QJsonObject& params = {});
    void sendRequest(const QString& method, const QJsonObject& params, int id);
    void respond(int id, const QJsonObject& result);
    void respondError(int id, int code, const QString& message);
    void processIncoming();
    void handleMessage(const QJsonObject& message);
    void handleResponse(int id, const QJsonObject& message);
    void handleServerRequest(int id, const QString& method, const QJsonObject& params);
    void handleNotification(const QString& method, const QJsonObject& params);
    void fail(const QString& message);
    void setState(State state);
    QString documentUri(const QString& path) const;
    static LspRange parseRange(const QJsonObject& range);
    static QString completionKind(int kind);

    QProcess* m_process = nullptr;
    QTimer* m_initializeTimer = nullptr;
    LspServerProfile m_profile;
    QString m_rootPath;
    QByteArray m_buffer;
    QHash<QString, int> m_versions;
    QHash<int, QString> m_completionRequests;
    QHash<int, QString> m_formattingRequests;
    State m_state = State::Stopped;
    int m_nextId = 1;
    int m_initializeId = 0;
    bool m_stopping = false;
    QJsonObject m_capabilities;
};
