#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct LspPosition {
    int line = 0;
    int character = 0;
};

struct LspRange {
    LspPosition start;
    LspPosition end;
};

struct LspCompletion {
    QString label;
    QString insertText;
    QString detail;
    QString kind;
};

struct LspTextEdit {
    LspRange range;
    QString newText;
};

struct LspDiagnostic {
    LspRange range;
    int severity = 1;
    QString message;
    QString source;
};

struct LspServerProfile {
    QString id;
    QString name;
    QString languageId;
    QStringList extensions;
    QString command;
    QStringList arguments;
    QJsonObject initializationOptions;
};
