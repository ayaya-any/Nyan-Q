#include "lsp/LspClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <QUrl>

namespace {
QString textOf(const QJsonValue& value)
{
    if (value.isString())
        return value.toString();
    if (value.isObject())
        return value.toObject().value(QStringLiteral("value")).toString();
    return {};
}
}  // namespace

LspClient::LspClient(QObject* parent)
    : QObject(parent)
{
    m_initializeTimer = new QTimer(this);
    m_initializeTimer->setSingleShot(true);
    m_initializeTimer->setInterval(15000);
    connect(m_initializeTimer, &QTimer::timeout, this, [this] {
        if (m_state == State::Starting)
            fail(QStringLiteral("语言服务器初始化超时"));
    });
}

LspClient::~LspClient()
{
    stop();
}

void LspClient::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

QString LspClient::documentUri(const QString& path) const
{
    return QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toString(QUrl::FullyEncoded);
}

void LspClient::start(const LspServerProfile& profile, const QString& rootPath)
{
    stop();
    m_profile = profile;
    m_rootPath = rootPath;
    m_versions.clear();
    m_completionRequests.clear();
    m_formattingRequests.clear();
    m_buffer.clear();
    m_stopping = false;
    m_nextId = 1;
    m_capabilities = {};

    if (profile.command.isEmpty()) {
        fail(QStringLiteral("未配置语言服务器命令"));
        return;
    }

    m_process = new QProcess(this);
    m_process->setProgram(profile.command);
    m_process->setArguments(profile.arguments);
    m_process->setWorkingDirectory(rootPath);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_process, &QProcess::started, this, [this] {
        setState(State::Starting);
        m_initializeTimer->start();
        m_initializeId = m_nextId++;

        const QUrl rootUri = QUrl::fromLocalFile(m_rootPath);
        QJsonObject capabilities{
            {QStringLiteral("workspace"),
             QJsonObject{{QStringLiteral("workspaceFolders"), true},
                         {QStringLiteral("configuration"), true}}},
            {QStringLiteral("textDocument"),
             QJsonObject{
                 {QStringLiteral("synchronization"),
                  QJsonObject{{QStringLiteral("dynamicRegistration"), false},
                              {QStringLiteral("willSave"), false},
                              {QStringLiteral("didSave"), true}}},
                 {QStringLiteral("completion"),
                  QJsonObject{{QStringLiteral("contextSupport"), true},
                              {QStringLiteral("completionItem"),
                               QJsonObject{{QStringLiteral("snippetSupport"), false},
                                           {QStringLiteral("documentationFormat"),
                                            QJsonArray{QStringLiteral("markdown"),
                                                       QStringLiteral("plaintext")}}}}}},
                 {QStringLiteral("formatting"), QJsonObject{}},
                 {QStringLiteral("publishDiagnostics"),
                  QJsonObject{{QStringLiteral("relatedInformation"), true}}},
             }},
        };

        QJsonObject params{
            {QStringLiteral("processId"), static_cast<int>(QCoreApplication::applicationPid())},
            {QStringLiteral("clientInfo"),
             QJsonObject{{QStringLiteral("name"), QStringLiteral("Nyan-Q")},
                         {QStringLiteral("version"), QStringLiteral("0.2")}}},
            {QStringLiteral("rootUri"), rootUri.toString(QUrl::FullyEncoded)},
            {QStringLiteral("rootPath"), QDir::toNativeSeparators(m_rootPath)},
            {QStringLiteral("capabilities"), capabilities},
            {QStringLiteral("workspaceFolders"),
             QJsonArray{QJsonObject{
                 {QStringLiteral("uri"), rootUri.toString(QUrl::FullyEncoded)},
                 {QStringLiteral("name"), QFileInfo(m_rootPath).fileName()}}}},
            {QStringLiteral("initializationOptions"), m_profile.initializationOptions},
        };
        sendRequest(QStringLiteral("initialize"), params, m_initializeId);
    });

    connect(m_process, &QProcess::readyReadStandardOutput, this,
            &LspClient::processIncoming);
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        const QString text = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
        if (!text.isEmpty())
            emit logMessage(text);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            fail(QStringLiteral("无法启动语言服务器：%1").arg(m_profile.command));
        }
    });
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                if (m_stopping || m_state == State::Stopped)
                    return;
                if (m_state == State::Starting || m_state == State::Ready) {
                    fail(QStringLiteral("语言服务器已退出（%1，%2）")
                             .arg(code)
                             .arg(status == QProcess::CrashExit ? QStringLiteral("崩溃")
                                                                : QStringLiteral("正常退出")));
                }
            });

    setState(State::Starting);
    m_process->start();
}

void LspClient::stop()
{
    m_initializeTimer->stop();
    m_stopping = true;
    if (m_process) {
        if (m_process->state() != QProcess::NotRunning) {
            if (m_state == State::Ready) {
                const int id = m_nextId++;
                sendRequest(QStringLiteral("shutdown"), {}, id);
                sendNotification(QStringLiteral("exit"));
                m_process->waitForFinished(300);
            }
            if (m_process->state() != QProcess::NotRunning) {
                m_process->kill();
                m_process->waitForFinished(300);
            }
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
    setState(State::Stopped);
}

void LspClient::sendMessage(const QJsonObject& message)
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    QByteArray packet = "Content-Length: " + QByteArray::number(body.size()) +
                        "\r\n\r\n" + body;
    m_process->write(packet);
}

void LspClient::sendNotification(const QString& method, const QJsonObject& params)
{
    QJsonObject message{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                        {QStringLiteral("method"), method}};
    if (!params.isEmpty())
        message.insert(QStringLiteral("params"), params);
    sendMessage(message);
}

void LspClient::sendRequest(const QString& method, const QJsonObject& params, int id)
{
    QJsonObject message{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                        {QStringLiteral("id"), id},
                        {QStringLiteral("method"), method}};
    if (!params.isEmpty())
        message.insert(QStringLiteral("params"), params);
    sendMessage(message);
}

void LspClient::respond(int id, const QJsonObject& result)
{
    sendMessage(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                            {QStringLiteral("id"), id},
                            {QStringLiteral("result"), result}});
}

void LspClient::respondError(int id, int code, const QString& message)
{
    sendMessage(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("error"),
         QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}},
    });
}

void LspClient::fail(const QString& message)
{
    m_initializeTimer->stop();
    emit errorMessage(message);
    setState(State::Failed);
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_stopping = true;
        m_process->kill();
    }
}

void LspClient::processIncoming()
{
    if (!m_process)
        return;
    m_buffer.append(m_process->readAllStandardOutput());

    while (true) {
        int headerEnd = m_buffer.indexOf("\r\n\r\n");
        int separatorSize = 4;
        if (headerEnd < 0) {
            headerEnd = m_buffer.indexOf("\n\n");
            separatorSize = 2;
        }
        if (headerEnd < 0)
            return;

        const QByteArray header = m_buffer.left(headerEnd);
        int contentLength = -1;
        for (const QByteArray& line : header.split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.toLower().startsWith("content-length:"))
                contentLength = trimmed.mid(15).trimmed().toInt();
        }
        if (contentLength < 0) {
            m_buffer.remove(0, headerEnd + separatorSize);
            continue;
        }
        const int total = headerEnd + separatorSize + contentLength;
        if (m_buffer.size() < total)
            return;

        const QByteArray body = m_buffer.mid(headerEnd + separatorSize, contentLength);
        m_buffer.remove(0, total);
        const QJsonDocument document = QJsonDocument::fromJson(body);
        if (document.isObject())
            handleMessage(document.object());
    }
}

void LspClient::handleMessage(const QJsonObject& message)
{
    if (message.contains(QStringLiteral("method"))) {
        const QString method = message.value(QStringLiteral("method")).toString();
        const QJsonObject params = message.value(QStringLiteral("params")).toObject();
        if (message.contains(QStringLiteral("id"))) {
            handleServerRequest(message.value(QStringLiteral("id")).toInt(), method, params);
        } else {
            handleNotification(method, params);
        }
        return;
    }

    if (message.contains(QStringLiteral("id")))
        handleResponse(message.value(QStringLiteral("id")).toInt(), message);
}

void LspClient::handleServerRequest(int id, const QString& method, const QJsonObject& params)
{
    if (method == QLatin1String("workspace/configuration")) {
        const QJsonArray items = params.value(QStringLiteral("items")).toArray();
        const QJsonObject yaml =
            m_profile.initializationOptions.value(QStringLiteral("yaml")).toObject();
        QJsonArray settings;
        for (const QJsonValue& value : items) {
            const QString section =
                value.toObject().value(QStringLiteral("section")).toString();
            if (section == QLatin1String("yaml") || section.startsWith(QLatin1String("yaml.")))
                settings.append(yaml);
            else
                settings.append(QJsonValue::Null);
        }
        sendMessage(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                {QStringLiteral("id"), id},
                                {QStringLiteral("result"), settings}});
    } else if (method == QLatin1String("workspace/workspaceFolders")) {
        const QString uri = QUrl::fromLocalFile(m_rootPath).toString(QUrl::FullyEncoded);
        sendMessage(QJsonObject{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), id},
            {QStringLiteral("result"),
             QJsonArray{QJsonObject{{QStringLiteral("uri"), uri},
                                    {QStringLiteral("name"),
                                     QFileInfo(m_rootPath).fileName()}}}},
        });
    } else if (method == QLatin1String("client/registerCapability") ||
               method == QLatin1String("window/workDoneProgress/create") ||
               method == QLatin1String("window/showMessageRequest")) {
        sendMessage(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                {QStringLiteral("id"), id},
                                {QStringLiteral("result"), QJsonValue::Null}});
    } else {
        Q_UNUSED(params)
        respondError(id, -32601, QStringLiteral("Unsupported request: %1").arg(method));
    }
}

void LspClient::handleNotification(const QString& method, const QJsonObject& params)
{
    if (method == QLatin1String("textDocument/publishDiagnostics")) {
        const QString path = QUrl(params.value(QStringLiteral("uri")).toString()).toLocalFile();
        QVector<LspDiagnostic> diagnostics;
        for (const QJsonValue& value :
             params.value(QStringLiteral("diagnostics")).toArray()) {
            const QJsonObject item = value.toObject();
            LspDiagnostic diagnostic;
            diagnostic.range = parseRange(item.value(QStringLiteral("range")).toObject());
            diagnostic.severity = item.value(QStringLiteral("severity")).toInt(1);
            diagnostic.message = item.value(QStringLiteral("message")).toString();
            diagnostic.source = item.value(QStringLiteral("source")).toString();
            diagnostics.push_back(diagnostic);
        }
        emit diagnosticsReady(path, diagnostics);
    } else if (method == QLatin1String("window/logMessage") ||
               method == QLatin1String("window/showMessage")) {
        const QString text = params.value(QStringLiteral("message")).toString();
        if (!text.isEmpty())
            emit logMessage(text);
    }
}

LspRange LspClient::parseRange(const QJsonObject& range)
{
    LspRange result;
    const QJsonObject start = range.value(QStringLiteral("start")).toObject();
    const QJsonObject end = range.value(QStringLiteral("end")).toObject();
    result.start.line = start.value(QStringLiteral("line")).toInt();
    result.start.character = start.value(QStringLiteral("character")).toInt();
    result.end.line = end.value(QStringLiteral("line")).toInt();
    result.end.character = end.value(QStringLiteral("character")).toInt();
    return result;
}

QString LspClient::completionKind(int kind)
{
    switch (kind) {
    case 2:
    case 3:
        return QStringLiteral("function");
    case 4:
        return QStringLiteral("constructor");
    case 5:
        return QStringLiteral("field");
    case 6:
        return QStringLiteral("variable");
    case 7:
        return QStringLiteral("class");
    case 8:
        return QStringLiteral("interface");
    case 9:
        return QStringLiteral("module");
    case 10:
        return QStringLiteral("property");
    case 12:
        return QStringLiteral("value");
    case 14:
        return QStringLiteral("keyword");
    case 15:
        return QStringLiteral("snippet");
    default:
        return QStringLiteral("text");
    }
}

void LspClient::handleResponse(int id, const QJsonObject& message)
{
    if (message.contains(QStringLiteral("error"))) {
        const QString error =
            message.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
        if (id == m_initializeId) {
            fail(QStringLiteral("语言服务器初始化失败：%1").arg(error));
        } else {
            emit logMessage(QStringLiteral("LSP 请求失败：%1").arg(error));
        }
        m_completionRequests.remove(id);
        m_formattingRequests.remove(id);
        return;
    }

    const QJsonValue resultValue = message.value(QStringLiteral("result"));
    if (id == m_initializeId) {
        m_initializeTimer->stop();
        m_capabilities = resultValue.toObject().value(QStringLiteral("capabilities")).toObject();
        sendNotification(QStringLiteral("initialized"), {});
        setState(State::Ready);
        emit ready();
        return;
    }

    if (m_completionRequests.contains(id)) {
        const QString path = m_completionRequests.take(id);
        QJsonArray items;
        if (resultValue.isArray()) {
            items = resultValue.toArray();
        } else if (resultValue.isObject()) {
            items = resultValue.toObject().value(QStringLiteral("items")).toArray();
        }
        QVector<LspCompletion> completions;
        for (const QJsonValue& value : items) {
            const QJsonObject item = value.toObject();
            LspCompletion completion;
            completion.label = item.value(QStringLiteral("label")).toString();
            completion.insertText = item.value(QStringLiteral("insertText")).toString();
            const QJsonObject textEdit = item.value(QStringLiteral("textEdit")).toObject();
            if (!textEdit.isEmpty())
                completion.insertText = textEdit.value(QStringLiteral("newText")).toString();
            if (completion.insertText.isEmpty())
                completion.insertText = completion.label;
            completion.detail = item.value(QStringLiteral("detail")).toString();
            if (completion.detail.isEmpty())
                completion.detail = textOf(item.value(QStringLiteral("documentation")));
            completion.kind = completionKind(item.value(QStringLiteral("kind")).toInt());
            if (!completion.label.isEmpty())
                completions.push_back(completion);
            if (completions.size() >= 200)
                break;
        }
        Q_UNUSED(path)
        emit completionReady(id, completions);
        return;
    }

    if (m_formattingRequests.contains(id)) {
        m_formattingRequests.remove(id);
        QVector<LspTextEdit> edits;
        for (const QJsonValue& value : resultValue.toArray()) {
            const QJsonObject item = value.toObject();
            LspTextEdit edit;
            edit.range = parseRange(item.value(QStringLiteral("range")).toObject());
            edit.newText = item.value(QStringLiteral("newText")).toString();
            edits.push_back(edit);
        }
        emit formattingReady(id, edits);
    }
}

void LspClient::openDocument(const QString& path, const QString& text)
{
    if (!isReady())
        return;
    m_versions[path] = 1;
    sendNotification(
        QStringLiteral("textDocument/didOpen"),
        QJsonObject{{QStringLiteral("textDocument"),
                     QJsonObject{{QStringLiteral("uri"), documentUri(path)},
                                 {QStringLiteral("languageId"), m_profile.languageId},
                                 {QStringLiteral("version"), 1},
                                 {QStringLiteral("text"), text}}}});
}

void LspClient::changeDocument(const QString& path, const QString& text)
{
    if (!isReady())
        return;
    const int version = ++m_versions[path];
    QJsonObject params{
        {QStringLiteral("textDocument"),
         QJsonObject{{QStringLiteral("uri"), documentUri(path)},
                     {QStringLiteral("version"), version}}},
        {QStringLiteral("contentChanges"),
         QJsonArray{QJsonObject{{QStringLiteral("text"), text}}}},
    };
    sendNotification(QStringLiteral("textDocument/didChange"), params);
}

void LspClient::saveDocument(const QString& path)
{
    if (!isReady())
        return;
    sendNotification(
        QStringLiteral("textDocument/didSave"),
        QJsonObject{{QStringLiteral("textDocument"),
                     QJsonObject{{QStringLiteral("uri"), documentUri(path)}}}});
}

void LspClient::closeDocument(const QString& path)
{
    if (!isReady())
        return;
    sendNotification(
        QStringLiteral("textDocument/didClose"),
        QJsonObject{{QStringLiteral("textDocument"),
                     QJsonObject{{QStringLiteral("uri"), documentUri(path)}}}});
    m_versions.remove(path);
}

int LspClient::requestCompletion(const QString& path, int line, int character)
{
    if (!isReady())
        return -1;
    const int id = m_nextId++;
    m_completionRequests.insert(id, path);
    sendRequest(
        QStringLiteral("textDocument/completion"),
        QJsonObject{{QStringLiteral("textDocument"),
                     QJsonObject{{QStringLiteral("uri"), documentUri(path)}}},
                    {QStringLiteral("position"),
                     QJsonObject{{QStringLiteral("line"), line},
                                 {QStringLiteral("character"), character}}},
                    {QStringLiteral("context"),
                     QJsonObject{{QStringLiteral("triggerKind"), 1}}}},
        id);
    return id;
}

int LspClient::requestFormatting(const QString& path)
{
    if (!isReady())
        return -1;
    const int id = m_nextId++;
    m_formattingRequests.insert(id, path);
    sendRequest(
        QStringLiteral("textDocument/formatting"),
        QJsonObject{{QStringLiteral("textDocument"),
                     QJsonObject{{QStringLiteral("uri"), documentUri(path)}}},
                    {QStringLiteral("options"),
                     QJsonObject{{QStringLiteral("tabSize"), 2},
                                 {QStringLiteral("insertSpaces"), true}}}},
        id);
    return id;
}
