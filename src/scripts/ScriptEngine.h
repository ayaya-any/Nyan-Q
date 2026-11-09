#pragma once

#include "scripts/ScriptTypes.h"

#include <QString>

#include <atomic>

namespace nq::scripts {

// Wraps an isolated QuickJS runtime/context. Every call creates a fresh runtime;
// a runtime is never shared between threads or between scripts.
//
// The engine exposes exactly one host capability: console.log/warn/error.
// There is no file, process, network, environment or module loader access.
class ScriptEngine {
public:
    // Cooperative cancellation for application shutdown. The pipeline owns the
    // flag; the QuickJS interrupt handler polls it.
    struct Control {
        std::atomic<bool> cancelRequested{false};
    };

    static ScriptExecutionResult execute(const ScriptExecutionRequest& request,
                                         Control* control = nullptr);

    // Compile-only syntax validation. No user code is executed.
    static bool validate(const QString& source, const QString& scriptName, QString* error,
                         int* line = nullptr, int* column = nullptr,
                         Control* control = nullptr);

    static QString engineVersion();
};

}  // namespace nq::scripts
