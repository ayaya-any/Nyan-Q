#include "scripts/ScriptEngine.h"

#include "quickjs.h"

#include <QByteArray>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace nq::scripts {
namespace {

constexpr qint64 kMaxValueString = 8192;
constexpr qint64 kMaxStackBytes = 512 * 1024;
constexpr int kMaxReturnDepth = 128;

using Clock = std::chrono::steady_clock;

struct RuntimeState {
    Clock::time_point deadline;
    ScriptEngine::Control* control = nullptr;
    bool timedOut = false;
    bool canceled = false;
    bool logLimitHit = false;
    int maxLogEntries = 1000;
    qint64 maxLogBytes = 1024 * 1024;
    qint64 logBytes = 0;
    QVector<ScriptLogLine> logs;
};

RuntimeState* stateOf(JSContext* ctx)
{
    return static_cast<RuntimeState*>(JS_GetContextOpaque(ctx));
}

bool deadlineReached(RuntimeState* state)
{
    return Clock::now() >= state->deadline;
}

int interruptHandler(JSContext* ctx, void* opaque)
{
    auto* state = static_cast<RuntimeState*>(opaque);
    if (!state)
        return 0;
    if (state->control &&
        state->control->cancelRequested.load(std::memory_order_relaxed)) {
        state->canceled = true;
        return 1;
    }
    if (Clock::now() >= state->deadline) {
        state->timedOut = true;
        return 1;
    }
    return 0;
}

JSModuleDef* moduleLoader(JSContext* ctx, const char* moduleName, void*)
{
    Q_UNUSED(moduleName)
    JS_ThrowTypeError(ctx, "模块加载已被禁用");
    return nullptr;
}

QString jsString(JSContext* ctx, JSValueConst value)
{
    size_t length = 0;
    const char* text = JS_ToCStringLen(ctx, &length, value);
    if (!text)
        return {};
    const QString result = QString::fromUtf8(text, static_cast<int>(length));
    JS_FreeCString(ctx, text);
    return result;
}

QString propertyString(JSContext* ctx, JSValueConst object, const char* name)
{
    JSValue value = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(value)) {
        JS_FreeValue(ctx, value);
        return {};
    }
    const QString result = jsString(ctx, value);
    JS_FreeValue(ctx, value);
    return result;
}

struct ErrorInfo {
    QString name;
    QString message;
    QString stack;
    int line = 0;
    int column = 0;
};

void parseLocation(const QString& text, int* line, int* column)
{
    static const QRegularExpression pattern(QStringLiteral(":(\\d+):(\\d+)"));
    auto match = pattern.match(text);
    if (match.hasMatch()) {
        bool okLine = false;
        bool okColumn = false;
        const int parsedLine = match.captured(1).toInt(&okLine);
        const int parsedColumn = match.captured(2).toInt(&okColumn);
        if (okLine)
            *line = parsedLine;
        if (okColumn)
            *column = parsedColumn;
    }
}

ErrorInfo describeValue(JSContext* ctx, JSValueConst value)
{
    ErrorInfo info;
    if (JS_IsObject(value)) {
        info.name = propertyString(ctx, value, "name");
        info.message = propertyString(ctx, value, "message");
        info.stack = propertyString(ctx, value, "stack");
        if (info.message.isEmpty()) {
            const QString text = jsString(ctx, value);
            if (!text.isEmpty() && text != QStringLiteral("[object Object]"))
                info.message = text;
        }
    } else if (JS_IsString(value)) {
        info.message = jsString(ctx, value);
    } else if (!JS_IsUndefined(value) && !JS_IsNull(value)) {
        info.message = jsString(ctx, value);
    }

    if (info.message.isEmpty())
        info.message = QStringLiteral("脚本抛出了未知异常");
    if (info.name.isEmpty())
        info.name = QStringLiteral("Error");
    parseLocation(info.stack, &info.line, &info.column);
    if (info.line == 0)
        parseLocation(info.message, &info.line, &info.column);
    return info;
}

ErrorInfo takeException(JSContext* ctx)
{
    JSValue exception = JS_GetException(ctx);
    const ErrorInfo info = describeValue(ctx, exception);
    JS_FreeValue(ctx, exception);
    return info;
}

void appendLog(RuntimeState* state, ScriptLogLevel level, QString message)
{
    if (state->logs.size() >= state->maxLogEntries) {
        if (!state->logLimitHit) {
            state->logLimitHit = true;
            state->logs.append(
                {ScriptLogLevel::Warning,
                 QStringLiteral("日志数量超过 %1 条，后续日志已被忽略").arg(state->maxLogEntries)});
        }
        return;
    }
    if (message.size() > 16384)
        message = message.left(16384) + QStringLiteral("…");
    const qint64 bytes = message.toUtf8().size();
    if (state->logBytes + bytes > state->maxLogBytes) {
        if (!state->logLimitHit) {
            state->logLimitHit = true;
            state->logs.append(
                {ScriptLogLevel::Warning, QStringLiteral("日志总大小超过上限，后续日志已被忽略")});
        }
        return;
    }
    state->logBytes += bytes;
    state->logs.append({level, std::move(message)});
}

QString formatConsoleValue(JSContext* ctx, JSValueConst value)
{
    if (JS_IsString(value)) {
        QString text = jsString(ctx, value);
        if (text.size() > 8192)
            text = text.left(8192) + QStringLiteral("…");
        return text;
    }
    if (JS_IsUndefined(value))
        return QStringLiteral("undefined");
    if (JS_IsNull(value))
        return QStringLiteral("null");
    if (JS_IsBool(value) || JS_IsNumber(value) || JS_IsBigInt(value)) {
        const QString text = jsString(ctx, value);
        return text.isEmpty() ? QStringLiteral("[值]") : text;
    }
    JSValue json = JS_JSONStringify(ctx, value, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(json)) {
        JS_FreeValue(ctx, json);
        JSValue exception = JS_GetException(ctx);
        JS_FreeValue(ctx, exception);
        return QStringLiteral("[无法序列化的值]");
    }
    if (JS_IsUndefined(json)) {
        JS_FreeValue(ctx, json);
        return QStringLiteral("undefined");
    }
    QString text = jsString(ctx, json);
    JS_FreeValue(ctx, json);
    if (text.size() > 8192)
        text = text.left(8192) + QStringLiteral("…");
    return text;
}

JSValue consoleWrite(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int magic)
{
    RuntimeState* state = stateOf(ctx);
    if (!state)
        return JS_UNDEFINED;

    ScriptLogLevel level = ScriptLogLevel::Info;
    switch (magic) {
    case 1:
        level = ScriptLogLevel::Warning;
        break;
    case 2:
        level = ScriptLogLevel::Error;
        break;
    case 3:
        level = ScriptLogLevel::Debug;
        break;
    default:
        break;
    }

    QStringList parts;
    parts.reserve(argc);
    for (int i = 0; i < argc; ++i)
        parts.append(formatConsoleValue(ctx, argv[i]));
    appendLog(state, level, parts.join(QLatin1Char(' ')));
    return JS_UNDEFINED;
}

bool installConsole(JSContext* ctx)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue console = JS_NewObject(ctx);
    if (JS_IsException(console)) {
        JS_FreeValue(ctx, global);
        return false;
    }

    struct Entry {
        const char* name;
        int magic;
    };
    const Entry entries[] = {
        {"log", 0}, {"info", 0}, {"warn", 1}, {"error", 2}, {"debug", 3}, {"trace", 3},
    };
    for (const Entry& entry : entries) {
        JSValue fn = JS_NewCFunctionMagic(ctx, consoleWrite, entry.name, 1,
                                          JS_CFUNC_generic_magic, entry.magic);
        if (JS_IsException(fn)) {
            JS_FreeValue(ctx, console);
            JS_FreeValue(ctx, global);
            return false;
        }
        JS_SetPropertyStr(ctx, console, entry.name, fn);
    }

    JS_SetPropertyStr(ctx, global, "console", console);
    JS_FreeValue(ctx, global);
    return true;
}

bool toJsValue(JSContext* ctx, const nq::yaml::Node& node, JSValue* output, int depth,
               QString* error)
{
    if (depth > kMaxReturnDepth) {
        *error = QStringLiteral("配置嵌套层级过深");
        return false;
    }
    switch (node.type) {
    case nq::yaml::Type::Null:
        *output = JS_NULL;
        return true;
    case nq::yaml::Type::Scalar:
        switch (node.scalarType) {
        case nq::yaml::ScalarType::Null:
            *output = JS_NULL;
            return true;
        case nq::yaml::ScalarType::Bool:
            *output = JS_NewBool(ctx, node.value == QStringLiteral("true"));
            return true;
        case nq::yaml::ScalarType::Int: {
            bool ok = false;
            const qint64 value = node.value.toLongLong(&ok);
            if (ok) {
                *output = JS_NewInt64(ctx, value);
            } else {
                const QByteArray utf8 = node.value.toUtf8();
                *output = JS_NewStringLen(ctx, utf8.constData(),
                                           static_cast<size_t>(utf8.size()));
            }
            return true;
        }
        case nq::yaml::ScalarType::Double: {
            bool ok = false;
            const double value = node.value.toDouble(&ok);
            if (ok) {
                *output = JS_NewFloat64(ctx, value);
            } else {
                const QByteArray utf8 = node.value.toUtf8();
                *output = JS_NewStringLen(ctx, utf8.constData(),
                                           static_cast<size_t>(utf8.size()));
            }
            return true;
        }
        case nq::yaml::ScalarType::String:
        default: {
            const QByteArray utf8 = node.value.toUtf8();
            *output = JS_NewStringLen(ctx, utf8.constData(), static_cast<size_t>(utf8.size()));
            return true;
        }
        }
    case nq::yaml::Type::Sequence: {
        JSValue array = JS_NewArray(ctx);
        if (JS_IsException(array)) {
            *error = QStringLiteral("无法在 JavaScript 中创建数组");
            return false;
        }
        for (int i = 0; i < node.sequence.size(); ++i) {
            JSValue child;
            if (!toJsValue(ctx, node.sequence.at(i), &child, depth + 1, error)) {
                JS_FreeValue(ctx, array);
                return false;
            }
            if (JS_SetPropertyUint32(ctx, array, static_cast<uint32_t>(i), child) < 0) {
                JS_FreeValue(ctx, array);
                *error = QStringLiteral("无法在 JavaScript 中写入数组元素");
                return false;
            }
        }
        *output = array;
        return true;
    }
    case nq::yaml::Type::Mapping: {
        JSValue object = JS_NewObject(ctx);
        if (JS_IsException(object)) {
            *error = QStringLiteral("无法在 JavaScript 中创建对象");
            return false;
        }
        for (const auto& entry : node.mapping) {
            JSValue child;
            if (!toJsValue(ctx, entry.second, &child, depth + 1, error)) {
                JS_FreeValue(ctx, object);
                return false;
            }
            const QByteArray key = entry.first.toUtf8();
            if (JS_SetPropertyStr(ctx, object, key.constData(), child) < 0) {
                JS_FreeValue(ctx, object);
                *error = QStringLiteral("无法在 JavaScript 中写入字段 %1").arg(entry.first);
                return false;
            }
        }
        *output = object;
        return true;
    }
    }
    *error = QStringLiteral("不支持的配置节点类型");
    return false;
}

struct ConvertState {
    JSContext* ctx = nullptr;
    JSValue objectPrototype{};
    QSet<const void*> stack;
    int depth = 0;
    QString error;
};

QString childPath(const QString& base, const QString& key)
{
    if (base.isEmpty())
        return key;
    return base + QLatin1Char('.') + key;
}

QString indexPath(const QString& base, int index)
{
    return QStringLiteral("%1[%2]").arg(base).arg(index);
}

bool fromJsValue(ConvertState& state, JSValueConst value, nq::yaml::Node* output,
                 const QString& path);

bool fromJsObject(ConvertState& state, JSValueConst value, nq::yaml::Node* output,
                  const QString& path)
{
    JSContext* ctx = state.ctx;
    JSValue prototype = JS_GetPrototype(ctx, value);
    const bool plain = JS_IsNull(prototype) ||
                       (JS_IsObject(prototype) && JS_VALUE_GET_PTR(prototype) ==
                                                       JS_VALUE_GET_PTR(state.objectPrototype));
    JS_FreeValue(ctx, prototype);
    if (!plain) {
        state.error = QStringLiteral("%1 返回了非普通对象（只允许对象、数组和 JSON 基础类型）")
                          .arg(path.isEmpty() ? QStringLiteral("main()") : path);
        return false;
    }

    const void* pointer = JS_VALUE_GET_PTR(value);
    if (state.stack.contains(pointer)) {
        state.error = QStringLiteral("%1 存在循环引用，无法转换为配置").arg(
            path.isEmpty() ? QStringLiteral("main() 返回值") : path);
        return false;
    }
    state.stack.insert(pointer);

    JSPropertyEnum* table = nullptr;
    uint32_t count = 0;
    if (JS_GetOwnPropertyNames(ctx, &table, &count, value,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
        state.stack.remove(pointer);
        const ErrorInfo info = takeException(ctx);
        state.error = QStringLiteral("读取 %1 的字段失败：%2")
                          .arg(path.isEmpty() ? QStringLiteral("main() 返回值") : path,
                               info.message);
        return false;
    }

    *output = nq::yaml::Node::object();
    for (uint32_t i = 0; i < count; ++i) {
        const JSAtom atom = table[i].atom;
        const char* keyText = JS_AtomToCString(ctx, atom);
        if (!keyText) {
            JS_FreePropertyEnum(ctx, table, count);
            state.stack.remove(pointer);
            state.error = QStringLiteral("无法读取对象字段名");
            return false;
        }
        const QString key = QString::fromUtf8(keyText);
        JS_FreeCString(ctx, keyText);

        JSValue child = JS_GetProperty(ctx, value, atom);
        if (JS_IsException(child)) {
            const ErrorInfo info = takeException(ctx);
            JS_FreePropertyEnum(ctx, table, count);
            state.stack.remove(pointer);
            state.error = QStringLiteral("读取 %1 失败：%2")
                              .arg(childPath(path, key), info.message);
            return false;
        }

        nq::yaml::Node childNode;
        const bool ok = fromJsValue(state, child, &childNode, childPath(path, key));
        const bool undefined = JS_IsUndefined(child);
        JS_FreeValue(ctx, child);
        if (!ok) {
            JS_FreePropertyEnum(ctx, table, count);
            state.stack.remove(pointer);
            return false;
        }
        if (!undefined)
            output->set(key, std::move(childNode));
    }
    JS_FreePropertyEnum(ctx, table, count);
    state.stack.remove(pointer);
    return true;
}

bool fromJsArray(ConvertState& state, JSValueConst value, nq::yaml::Node* output,
                 const QString& path)
{
    JSContext* ctx = state.ctx;
    const void* pointer = JS_VALUE_GET_PTR(value);
    if (state.stack.contains(pointer)) {
        state.error = QStringLiteral("%1 存在循环引用，无法转换为配置").arg(path);
        return false;
    }
    state.stack.insert(pointer);

    JSValue lengthValue = JS_GetPropertyStr(ctx, value, "length");
    double lengthNumber = 0;
    if (JS_IsException(lengthValue) || JS_ToFloat64(ctx, &lengthNumber, lengthValue) < 0) {
        JS_FreeValue(ctx, lengthValue);
        state.stack.remove(pointer);
        state.error = QStringLiteral("读取 %1 的数组长度失败").arg(path);
        return false;
    }
    JS_FreeValue(ctx, lengthValue);
    if (!std::isfinite(lengthNumber) || lengthNumber < 0 || lengthNumber > 1000000) {
        state.stack.remove(pointer);
        state.error = QStringLiteral("%1 的数组长度无效").arg(path);
        return false;
    }

    *output = nq::yaml::Node::array();
    const int length = static_cast<int>(lengthNumber);
    output->sequence.reserve(length);
    for (int i = 0; i < length; ++i) {
        JSValue child = JS_GetPropertyUint32(ctx, value, static_cast<uint32_t>(i));
        if (JS_IsException(child)) {
            const ErrorInfo info = takeException(ctx);
            state.stack.remove(pointer);
            state.error = QStringLiteral("读取 %1 失败：%2").arg(indexPath(path, i), info.message);
            return false;
        }
        nq::yaml::Node childNode;
        const bool ok = fromJsValue(state, child, &childNode, indexPath(path, i));
        const bool undefined = JS_IsUndefined(child);
        JS_FreeValue(ctx, child);
        if (!ok) {
            state.stack.remove(pointer);
            return false;
        }
        output->sequence.append(undefined ? nq::yaml::Node::null() : std::move(childNode));
    }
    state.stack.remove(pointer);
    return true;
}

bool fromJsValue(ConvertState& state, JSValueConst value, nq::yaml::Node* output,
                 const QString& path)
{
    JSContext* ctx = state.ctx;
    if (++state.depth > kMaxReturnDepth) {
        state.error = QStringLiteral("main() 返回的配置嵌套层级过深");
        return false;
    }

    const QString where = path.isEmpty() ? QStringLiteral("main() 返回值") : path;
    if (JS_IsNull(value)) {
        *output = nq::yaml::Node::null();
    } else if (JS_IsUndefined(value)) {
        *output = nq::yaml::Node::null();
    } else if (JS_IsBool(value)) {
        *output = nq::yaml::Node::boolean(JS_ToBool(ctx, value) == 1);
    } else if (JS_IsNumber(value)) {
        double number = 0;
        if (JS_ToFloat64(ctx, &number, value) < 0) {
            state.error = QStringLiteral("%1 不是有效数字").arg(where);
            return false;
        }
        if (!std::isfinite(number)) {
            state.error = QStringLiteral("%1 包含 NaN 或 Infinity，配置中不允许").arg(where);
            return false;
        }
        if (std::floor(number) == number &&
            number >= static_cast<double>(std::numeric_limits<qint64>::min()) &&
            number <= static_cast<double>(std::numeric_limits<qint64>::max())) {
            const qint64 integer = static_cast<qint64>(number);
            *output = nq::yaml::Node::integer(integer);
        } else {
            *output = nq::yaml::Node::number(number);
        }
    } else if (JS_IsString(value)) {
        *output = nq::yaml::Node::string(jsString(ctx, value));
    } else if (JS_IsFunction(ctx, value)) {
        state.error = QStringLiteral("%1 是函数，配置中只允许 JSON 数据类型").arg(where);
        return false;
    } else if (JS_IsArray(value)) {
        if (!fromJsArray(state, value, output, where))
            return false;
    } else if (JS_IsObject(value)) {
        if (!fromJsObject(state, value, output, where))
            return false;
    } else if (JS_IsSymbol(value)) {
        state.error = QStringLiteral("%1 是 Symbol，配置中只允许 JSON 数据类型").arg(where);
        return false;
    } else if (JS_IsBigInt(value)) {
        state.error = QStringLiteral("%1 是 BigInt，配置中只允许字符串或数字").arg(where);
        return false;
    } else {
        state.error = QStringLiteral("%1 的类型不受支持").arg(where);
        return false;
    }

    --state.depth;
    return true;
}

bool isMemoryError(const QString& text)
{
    const QString lower = text.toLower();
    return lower.contains(QStringLiteral("out of memory")) ||
           lower.contains(QStringLiteral("allocation failed")) ||
           text.contains(QStringLiteral("内存不足"));
}

ScriptExecutionResult makeFailure(const RuntimeState& state, ScriptRunStatus status,
                                  const QString& message)
{
    ScriptExecutionResult result;
    result.ok = false;
    result.status = status;
    result.errorName = status == ScriptRunStatus::TimedOut ? QStringLiteral("Timeout")
                                                           : QStringLiteral("Error");
    result.errorMessage = message;
    result.logs = state.logs;
    return result;
}

struct RuntimeGuard {
    JSRuntime* runtime = nullptr;
    JSContext* context = nullptr;

    ~RuntimeGuard()
    {
        if (context)
            JS_FreeContext(context);
        if (runtime)
            JS_FreeRuntime(runtime);
    }
};

}  // namespace

QString ScriptEngine::engineVersion()
{
    return QString::fromLatin1(JS_GetVersion());
}

ScriptExecutionResult ScriptEngine::execute(const ScriptExecutionRequest& request,
                                            Control* control)
{
    const auto started = Clock::now();
    ScriptExecutionResult result;
    result.status = ScriptRunStatus::Failed;
    result.config = request.config;

    if (request.source.trimmed().isEmpty()) {
        result.errorName = QStringLiteral("SyntaxError");
        result.errorMessage = QStringLiteral("脚本内容为空");
        return result;
    }
    if (request.source.size() > 512 * 1024) {
        result.errorName = QStringLiteral("RangeError");
        result.errorMessage = QStringLiteral("脚本内容超过 512 KB 限制");
        return result;
    }

    RuntimeState state;
    state.control = control;
    state.deadline = started + std::chrono::milliseconds(qBound(100, request.limits.timeoutMs, 120000));
    state.maxLogEntries = qMax(1, request.limits.maxLogEntries);
    state.maxLogBytes = qMax<qint64>(1024, request.limits.maxLogBytes);

    if (control && control->cancelRequested.load(std::memory_order_relaxed)) {
        result.status = ScriptRunStatus::Canceled;
        result.errorName = QStringLiteral("Canceled");
        result.errorMessage = QStringLiteral("脚本执行已取消");
        result.durationMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
        return result;
    }

    RuntimeGuard guard;
    guard.runtime = JS_NewRuntime();
    if (!guard.runtime) {
        result.errorName = QStringLiteral("InternalError");
        result.errorMessage = QStringLiteral("无法创建 QuickJS 运行时");
        result.durationMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
        return result;
    }

    JS_SetMemoryLimit(guard.runtime,
                      static_cast<size_t>(qBound<qint64>(8LL * 1024 * 1024,
                                                         request.limits.memoryLimitBytes,
                                                         2048LL * 1024 * 1024)));
    JS_SetMaxStackSize(guard.runtime, static_cast<size_t>(kMaxStackBytes));
    JS_SetCanBlock(guard.runtime, false);
    JS_SetInterruptHandler(guard.runtime, interruptHandler, &state);
    JS_SetModuleLoaderFunc(guard.runtime, nullptr, moduleLoader, nullptr);

    guard.context = JS_NewContext(guard.runtime);
    if (!guard.context) {
        result.status = state.timedOut ? ScriptRunStatus::TimedOut : ScriptRunStatus::Failed;
        result.errorName = QStringLiteral("InternalError");
        result.errorMessage = QStringLiteral("无法创建 QuickJS 上下文");
        result.logs = state.logs;
        result.durationMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
        return result;
    }
    JSContext* ctx = guard.context;
    JS_SetContextOpaque(ctx, &state);

    if (!installConsole(ctx)) {
        const ErrorInfo info = takeException(ctx);
        result.status = state.timedOut ? ScriptRunStatus::TimedOut : ScriptRunStatus::Failed;
        result.errorName = info.name;
        result.errorMessage = QStringLiteral("初始化 console 失败：%1").arg(info.message);
        result.logs = state.logs;
        result.durationMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
        return result;
    }

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue objectCtor = JS_GetPropertyStr(ctx, global, "Object");
    JSValue objectPrototype;
    if (JS_IsException(objectCtor)) {
        takeException(ctx);
        objectPrototype = JS_UNDEFINED;
    } else {
        objectPrototype = JS_GetPropertyStr(ctx, objectCtor, "prototype");
        if (JS_IsException(objectPrototype)) {
            takeException(ctx);
            objectPrototype = JS_UNDEFINED;
        }
    }
    JS_FreeValue(ctx, objectCtor);

    auto finalize = [&](ScriptRunStatus status, const QString& name, const QString& message,
                        const QString& stack = {}, int line = 0, int column = 0) {
        result.ok = status == ScriptRunStatus::Success;
        result.status = status;
        result.errorName = name;
        result.errorMessage = message;
        result.stack = stack;
        result.line = line;
        result.column = column;
        result.logs = state.logs;
        result.durationMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    };

    // 1. Evaluate the user's script in a fresh global context.
    const QByteArray source = request.source.toUtf8();
    QString fileName = request.scriptName.trimmed();
    fileName.replace(QLatin1Char('\n'), QLatin1Char(' '));
    fileName.replace(QLatin1Char('\r'), QLatin1Char(' '));
    if (fileName.isEmpty())
        fileName = QStringLiteral("script");
    const QByteArray filename = QStringLiteral("<script:%1>").arg(fileName).toUtf8();

    JSValue evaluated = JS_Eval(ctx, source.constData(), static_cast<size_t>(source.size()),
                                 filename.constData(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(evaluated)) {
        const ErrorInfo info = takeException(ctx);
        JS_FreeValue(ctx, global);
        JS_FreeValue(ctx, objectPrototype);
        if (state.canceled) {
            finalize(ScriptRunStatus::Canceled, QStringLiteral("Canceled"),
                     QStringLiteral("脚本执行已取消"));
        } else if (state.timedOut) {
            finalize(ScriptRunStatus::TimedOut, QStringLiteral("Timeout"),
                     QStringLiteral("脚本编译超过 %1 ms，已被中断")
                         .arg(request.limits.timeoutMs));
        } else {
            finalize(isMemoryError(info.message) ? ScriptRunStatus::MemoryExceeded
                                                 : ScriptRunStatus::Failed,
                     info.name, info.message, info.stack, info.line, info.column);
        }
        return result;
    }
    JS_FreeValue(ctx, evaluated);

    // 2. Resolve main. Evaluating the identifier (instead of reading a global
    // property) also accepts `const main = ...` and arrow functions.
    JSValue entry = JS_Eval(ctx, "main", 4, "<script-entry>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(entry)) {
        takeException(ctx);
        JS_FreeValue(ctx, global);
        JS_FreeValue(ctx, objectPrototype);
        finalize(ScriptRunStatus::Failed, QStringLiteral("TypeError"),
                 QStringLiteral("脚本没有定义 main(config, profileName) 函数"));
        return result;
    }
    if (!JS_IsFunction(ctx, entry)) {
        JS_FreeValue(ctx, entry);
        JS_FreeValue(ctx, global);
        JS_FreeValue(ctx, objectPrototype);
        finalize(ScriptRunStatus::Failed, QStringLiteral("TypeError"),
                 QStringLiteral("main 不是函数，脚本需要定义 main(config, profileName)"));
        return result;
    }

    // 3. Convert the YAML configuration to a JavaScript object.
    JSValue configValue;
    QString conversionError;
    if (!toJsValue(ctx, request.config, &configValue, 0, &conversionError)) {
        JS_FreeValue(ctx, entry);
        JS_FreeValue(ctx, global);
        JS_FreeValue(ctx, objectPrototype);
        finalize(ScriptRunStatus::Failed, QStringLiteral("TypeError"), conversionError);
        return result;
    }

    const QByteArray profileName = request.profileName.toUtf8();
    JSValue profileValue =
        JS_NewStringLen(ctx, profileName.constData(), static_cast<size_t>(profileName.size()));
    JSValue args[2] = {configValue, profileValue};
    JSValue callResult = JS_Call(ctx, entry, JS_UNDEFINED, 2, args);
    JS_FreeValue(ctx, profileValue);
    JS_FreeValue(ctx, configValue);
    JS_FreeValue(ctx, entry);

    if (JS_IsException(callResult)) {
        const ErrorInfo info = takeException(ctx);
        JS_FreeValue(ctx, global);
        JS_FreeValue(ctx, objectPrototype);
        if (state.canceled) {
            finalize(ScriptRunStatus::Canceled, QStringLiteral("Canceled"),
                     QStringLiteral("脚本执行已取消"));
        } else if (state.timedOut) {
            finalize(ScriptRunStatus::TimedOut, QStringLiteral("Timeout"),
                     QStringLiteral("脚本执行超过 %1 ms，已被强制中断")
                         .arg(request.limits.timeoutMs));
        } else {
            finalize(isMemoryError(info.message) ? ScriptRunStatus::MemoryExceeded
                                                 : ScriptRunStatus::Failed,
                     info.name, info.message, info.stack, info.line, info.column);
        }
        return result;
    }

    // 4. Await a returned Promise using QuickJS' job queue. There is no host
    // timer or I/O capability, so an empty job queue means it can never settle.
    if (JS_IsPromise(callResult)) {
        while (JS_PromiseState(ctx, callResult) == JS_PROMISE_PENDING) {
            if (control &&
                control->cancelRequested.load(std::memory_order_relaxed)) {
                state.canceled = true;
                break;
            }
            if (deadlineReached(&state)) {
                state.timedOut = true;
                break;
            }
            JSContext* jobContext = nullptr;
            const int jobResult = JS_ExecutePendingJob(guard.runtime, &jobContext);
            if (jobResult < 0) {
                const ErrorInfo info = takeException(ctx);
                if (JS_PromiseState(ctx, callResult) != JS_PROMISE_REJECTED) {
                    JS_FreeValue(ctx, callResult);
                    JS_FreeValue(ctx, global);
                    JS_FreeValue(ctx, objectPrototype);
                    finalize(isMemoryError(info.message) ? ScriptRunStatus::MemoryExceeded
                                                         : ScriptRunStatus::Failed,
                             info.name, info.message, info.stack, info.line, info.column);
                    return result;
                }
            } else if (jobResult == 0) {
                // Nothing left to run: a promise waiting for a timer/socket can
                // never complete because no such host API exists.
                JS_FreeValue(ctx, callResult);
                JS_FreeValue(ctx, global);
                JS_FreeValue(ctx, objectPrototype);
                finalize(ScriptRunStatus::Failed, QStringLiteral("TimeoutError"),
                         QStringLiteral("main() 返回的 Promise 永远不会完成："
                                        "脚本没有可用的定时器、I/O 或网络接口"));
                return result;
            }
        }

        if (state.canceled) {
            JS_FreeValue(ctx, callResult);
            JS_FreeValue(ctx, global);
            JS_FreeValue(ctx, objectPrototype);
            finalize(ScriptRunStatus::Canceled, QStringLiteral("Canceled"),
                     QStringLiteral("脚本执行已取消"));
            return result;
        }
        if (state.timedOut) {
            JS_FreeValue(ctx, callResult);
            JS_FreeValue(ctx, global);
            JS_FreeValue(ctx, objectPrototype);
            finalize(ScriptRunStatus::TimedOut, QStringLiteral("Timeout"),
                     QStringLiteral("main() 返回的 Promise 在 %1 ms 内未完成")
                         .arg(request.limits.timeoutMs));
            return result;
        }

        const JSPromiseStateEnum promiseState = JS_PromiseState(ctx, callResult);
        if (promiseState == JS_PROMISE_REJECTED) {
            JSValue rejection = JS_PromiseResult(ctx, callResult);
            const ErrorInfo info = describeValue(ctx, rejection);
            JS_FreeValue(ctx, rejection);
            JS_FreeValue(ctx, callResult);
            JS_FreeValue(ctx, global);
            JS_FreeValue(ctx, objectPrototype);
            finalize(ScriptRunStatus::Failed, info.name, info.message, info.stack, info.line,
                     info.column);
            return result;
        }
        if (promiseState != JS_PROMISE_FULFILLED) {
            JS_FreeValue(ctx, callResult);
            JS_FreeValue(ctx, global);
            JS_FreeValue(ctx, objectPrototype);
            finalize(ScriptRunStatus::Failed, QStringLiteral("TimeoutError"),
                     QStringLiteral("main() 返回的 Promise 未完成"));
            return result;
        }

        JSValue fulfilled = JS_PromiseResult(ctx, callResult);
        JS_FreeValue(ctx, callResult);
        callResult = fulfilled;
    }

    // 5. Validate and convert the returned value. A successful script must
    // return a configuration object; undefined/null are rejected explicitly.
    if (JS_IsUndefined(callResult) || JS_IsNull(callResult)) {
        const bool wasNull = JS_IsNull(callResult);
        JS_FreeValue(ctx, callResult);
        JS_FreeValue(ctx, global);
        JS_FreeValue(ctx, objectPrototype);
        finalize(ScriptRunStatus::Failed, QStringLiteral("TypeError"),
                 QStringLiteral("main() 必须返回完整的配置对象，而不是 %1")
                     .arg(wasNull ? QStringLiteral("null") : QStringLiteral("undefined")));
        return result;
    }
    if (!JS_IsObject(callResult) || JS_IsFunction(ctx, callResult) ||
        JS_IsArray(callResult)) {
        JS_FreeValue(ctx, callResult);
        JS_FreeValue(ctx, global);
        JS_FreeValue(ctx, objectPrototype);
        finalize(ScriptRunStatus::Failed, QStringLiteral("TypeError"),
                 QStringLiteral("main() 必须返回 YAML 配置对象（顶层为对象）"));
        return result;
    }

    ConvertState convert;
    convert.ctx = ctx;
    convert.objectPrototype = objectPrototype;
    nq::yaml::Node output;
    const bool converted = fromJsValue(convert, callResult, &output, QString());
    JS_FreeValue(ctx, callResult);
    JS_FreeValue(ctx, global);
    JS_FreeValue(ctx, objectPrototype);

    if (!converted) {
        finalize(ScriptRunStatus::Failed, QStringLiteral("TypeError"), convert.error);
        return result;
    }
    if (!output.isMapping()) {
        finalize(ScriptRunStatus::Failed, QStringLiteral("TypeError"),
                 QStringLiteral("main() 必须返回配置对象"));
        return result;
    }

    result.ok = true;
    result.status = ScriptRunStatus::Success;
    result.config = std::move(output);
    result.logs = state.logs;
    const nq::yaml::DiffResult diff = nq::yaml::diff(request.config, result.config);
    result.changed = !diff.entries.isEmpty();
    result.durationMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    return result;
}

bool ScriptEngine::validate(const QString& source, const QString& scriptName, QString* error,
                            int* line, int* column, Control* control)
{
    if (error)
        error->clear();
    if (line)
        *line = 0;
    if (column)
        *column = 0;
    if (source.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("脚本内容为空");
        return false;
    }
    if (source.size() > 512 * 1024) {
        if (error)
            *error = QStringLiteral("脚本内容超过 512 KB 限制");
        return false;
    }

    RuntimeState state;
    state.control = control;
    state.deadline = Clock::now() + std::chrono::seconds(2);

    RuntimeGuard guard;
    guard.runtime = JS_NewRuntime();
    if (!guard.runtime) {
        if (error)
            *error = QStringLiteral("无法创建 QuickJS 运行时");
        return false;
    }
    JS_SetMemoryLimit(guard.runtime, 64LL * 1024 * 1024);
    JS_SetMaxStackSize(guard.runtime, static_cast<size_t>(kMaxStackBytes));
    JS_SetCanBlock(guard.runtime, false);
    JS_SetInterruptHandler(guard.runtime, interruptHandler, &state);
    JS_SetModuleLoaderFunc(guard.runtime, nullptr, moduleLoader, nullptr);

    guard.context = JS_NewContext(guard.runtime);
    if (!guard.context) {
        if (error)
            *error = QStringLiteral("无法创建 QuickJS 上下文");
        return false;
    }
    JSContext* ctx = guard.context;
    JS_SetContextOpaque(ctx, &state);

    QString name = scriptName.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("script");
    const QByteArray filename = QStringLiteral("<script:%1>").arg(name).toUtf8();
    const QByteArray bytes = source.toUtf8();
    JSValue compiled = JS_Eval(ctx, bytes.constData(), static_cast<size_t>(bytes.size()),
                               filename.constData(),
                               JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(compiled)) {
        const ErrorInfo info = takeException(ctx);
        if (error)
            *error = info.message;
        if (line)
            *line = info.line;
        if (column)
            *column = info.column;
        return false;
    }
    JS_FreeValue(ctx, compiled);
    return true;
}

}  // namespace nq::scripts
