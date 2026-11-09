#pragma once

#include "scripts/ScriptTypes.h"

#include <QString>
#include <QVector>

namespace nq::scripts {

// In-memory run/update log with a bounded JSON-lines file on disk. All methods
// are used from the thread that owns ScriptManager.
class ScriptLogManager {
public:
    explicit ScriptLogManager(QString filePath = {}, int capacity = 2000);

    void setFilePath(const QString& path) { m_filePath = path; }
    QString filePath() const { return m_filePath; }
    void setCapacity(int capacity);
    void loadExisting();

    void append(ScriptLogLevel level, const QString& scriptId, const QString& scriptName,
                const QString& message);
    void append(const ScriptLogEntry& entry);

    void recordRun(const QString& profileName, const ScriptStepResult& step);
    void recordUpdate(const ScriptUpdateResult& update);

    QVector<ScriptLogEntry> entries() const { return m_entries; }
    QVector<ScriptLogEntry> entriesFor(const QString& scriptId) const;
    void clear();

private:
    void rotateIfNeeded();

    QString m_filePath;
    int m_capacity = 2000;
    QVector<ScriptLogEntry> m_entries;
};

}  // namespace nq::scripts
