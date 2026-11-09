#pragma once

#include "scripts/ScriptTypes.h"
#include "ui/dialogs/MaterialDialog.h"

#include <QByteArray>
#include <QDialog>
#include <QString>
#include <QUrl>
#include <QVector>

class ClashController;
class M3Button;
class M3Switch;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QSpinBox;
class QTableWidget;
class QTabWidget;

namespace nq::scripts {
class ScriptManager;
class ScriptPipeline;
}  // namespace nq::scripts

class ScriptUrlDialog final : public MaterialDialog {
    Q_OBJECT

public:
    explicit ScriptUrlDialog(QWidget* parent = nullptr);

    QString scriptName() const;
    QUrl scriptUrl() const;

private:
    void tryAccept();

    QLineEdit* m_name = nullptr;
    QLineEdit* m_url = nullptr;
    QCheckBox* m_trust = nullptr;
    QLabel* m_preview = nullptr;
};

class ScriptNameDialog final : public MaterialDialog {
    Q_OBJECT

public:
    explicit ScriptNameDialog(QWidget* parent = nullptr, const QString& initialName = {});

    QString scriptName() const;

private:
    QLineEdit* m_name = nullptr;
};

class ScriptBindingsDialog final : public MaterialDialog {
    Q_OBJECT

public:
    ScriptBindingsDialog(ClashController* controller, const QString& scriptId,
                         QWidget* parent = nullptr);

private:
    void applyBindings();

    ClashController* m_controller = nullptr;
    QString m_scriptId;
    QCheckBox* m_global = nullptr;
    QVector<QCheckBox*> m_profileChecks;
    QVector<QString> m_profileIds;
};

class ScriptHistoryDialog final : public MaterialDialog {
    Q_OBJECT

public:
    ScriptHistoryDialog(nq::scripts::ScriptManager* manager, const QString& scriptId,
                        QWidget* parent = nullptr);

private:
    void rebuild();
    void showSelected();
    void rollbackSelected();

    nq::scripts::ScriptManager* m_manager = nullptr;
    QString m_scriptId;
    QListWidget* m_list = nullptr;
    QPlainTextEdit* m_preview = nullptr;
    QLabel* m_details = nullptr;
    QLabel* m_feedback = nullptr;
};

class ScriptLogsDialog final : public MaterialDialog {
    Q_OBJECT

public:
    ScriptLogsDialog(nq::scripts::ScriptManager* manager, const QString& scriptId,
                     QWidget* parent = nullptr);

private:
    void refresh();

    nq::scripts::ScriptManager* m_manager = nullptr;
    QString m_scriptId;
    QPlainTextEdit* m_view = nullptr;
};

class ScriptSettingsDialog final : public MaterialDialog {
    Q_OBJECT

public:
    explicit ScriptSettingsDialog(nq::scripts::ScriptManager* manager,
                                  QWidget* parent = nullptr);

private:
    void save();

    nq::scripts::ScriptManager* m_manager = nullptr;
    QSpinBox* m_timeout = nullptr;
    QSpinBox* m_memory = nullptr;
    QCheckBox* m_stopOnError = nullptr;
    QCheckBox* m_autoUpdate = nullptr;
    QSpinBox* m_logRetention = nullptr;
};

class ScriptTestDialog final : public MaterialDialog {
    Q_OBJECT

public:
    ScriptTestDialog(ClashController* controller, const QString& scriptName,
                     const QByteArray& source, QWidget* parent = nullptr);
    ScriptTestDialog(ClashController* controller, const QVector<nq::scripts::ScriptStep>& steps,
                     const QString& title, QWidget* parent = nullptr);
    ~ScriptTestDialog() override;

    // Development/screenshot helper: starts the same asynchronous test run as
    // the button. It does not change any production behavior.
    void startTestForScreenshot() { run(); }

private:
    void setup(const QVector<nq::scripts::ScriptStep>& steps, const QString& title);
    void run();
    void handleResult(const nq::scripts::ScriptPipelineResult& result);
    void showError(const QString& message);

    ClashController* m_controller = nullptr;
    QString m_scriptName;
    QVector<nq::scripts::ScriptStep> m_steps;
    QComboBox* m_profiles = nullptr;
    M3Button* m_run = nullptr;
    QLabel* m_status = nullptr;
    QTableWidget* m_diff = nullptr;
    QPlainTextEdit* m_error = nullptr;
    QPlainTextEdit* m_before = nullptr;
    QPlainTextEdit* m_after = nullptr;
    QPlainTextEdit* m_logs = nullptr;
    QTabWidget* m_tabs = nullptr;
    nq::scripts::ScriptPipeline* m_pipeline = nullptr;
};

class ScriptUpdateDiffDialog final : public MaterialDialog {
    Q_OBJECT

public:
    explicit ScriptUpdateDiffDialog(const nq::scripts::ScriptUpdateResult& update,
                                    QWidget* parent = nullptr);
};
