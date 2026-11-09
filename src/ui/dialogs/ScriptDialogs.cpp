#include "ui/dialogs/ScriptDialogs.h"

#include "app/Fonts.h"
#include "app/Theme.h"
#include "core/ClashController.h"
#include "core/ProfileStore.h"
#include "scripts/ScriptDownloader.h"
#include "scripts/ScriptManager.h"
#include "scripts/ScriptPipeline.h"
#include "ui/dialogs/ConfirmDialog.h"
#include "ui/widgets/MaterialWidgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

using namespace nq::scripts;

namespace {

QLabel* roleLabel(const QString& text, const char* role, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", role);
    label->setWordWrap(true);
    return label;
}

QPlainTextEdit* readOnlyCode(QWidget* parent)
{
    auto* view = new QPlainTextEdit(parent);
    view->setReadOnly(true);
    view->setFont(Fonts::mono(12));
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setMinimumHeight(180);
    return view;
}

QString formatTime(const QDateTime& value)
{
    return value.isValid() ? value.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                           : QStringLiteral("从未");
}

QString logLine(const ScriptLogEntry& entry)
{
    return QStringLiteral("%1  %2  %3  %4")
        .arg(entry.time.toString(QStringLiteral("MM-dd HH:mm:ss")),
             logLevelLabel(entry.level),
             entry.scriptName.isEmpty() ? QStringLiteral("-") : entry.scriptName,
             entry.message);
}

}  // namespace

ScriptUrlDialog::ScriptUrlDialog(QWidget* parent)
    : MaterialDialog(parent, QStringLiteral("导入远程 JavaScript"),
                     QStringLiteral("远程脚本可以修改代理节点、DNS 和路由规则，"
                                    "请只导入你信任的来源。"))
{
    setDialogWidth(620);

    auto* warning = roleLabel(
        QStringLiteral("远程脚本会在本机以受限的 QuickJS 沙箱执行，但它仍能改变流量走向。"
                       "自动更新默认关闭；更新会先下载到临时缓存，校验成功后才替换旧版本。"),
        "secondary", this);
    contentLayout()->addWidget(warning);
    contentLayout()->addSpacing(4);

    auto* nameLabel = roleLabel(QStringLiteral("脚本名称（可选）"), "bodyStrong", this);
    contentLayout()->addWidget(nameLabel);
    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(QStringLiteral("例如：自动测速与规则整理"));
    m_name->setMaxLength(80);
    contentLayout()->addWidget(m_name);

    contentLayout()->addWidget(roleLabel(QStringLiteral("HTTPS 脚本地址"), "bodyStrong", this));
    m_url = new QLineEdit(this);
    m_url->setPlaceholderText(QStringLiteral("https://example.com/nyanq.js"));
    m_url->setClearButtonEnabled(true);
    contentLayout()->addWidget(m_url);

    m_preview = roleLabel(QString(), "micro", this);
    contentLayout()->addWidget(m_preview);

    m_trust = new QCheckBox(
        QStringLiteral("我已核对上方来源 URL，并确认信任该脚本"), this);
    contentLayout()->addWidget(m_trust);

    auto* hint = roleLabel(
        QStringLiteral("仅支持 HTTPS（回环地址可用于本地开发）。脚本不能自行发起网络请求，"
                       "也不能读取文件或环境变量。"),
        "micro", this);
    contentLayout()->addWidget(hint);

    auto* cancel = new M3Button(QStringLiteral("取消"), M3Button::Variant::Text, this);
    auto* import = new M3Button(QStringLiteral("下载并导入"), M3Button::Variant::Filled, this);
    import->setIconText(QStringLiteral("download"));
    footerLayout()->addWidget(cancel);
    footerLayout()->addWidget(import);
    setPrimaryAction(import);

    connect(cancel, &QAbstractButton::clicked, this, &QDialog::reject);
    connect(import, &QAbstractButton::clicked, this, &ScriptUrlDialog::tryAccept);
    connect(m_url, &QLineEdit::returnPressed, this, &ScriptUrlDialog::tryAccept);
    connect(m_url, &QLineEdit::textChanged, this, [this](const QString& text) {
        clearError();
        const QUrl url(text.trimmed());
        m_preview->setText(text.trimmed().isEmpty()
                               ? QString()
                               : QStringLiteral("来源：%1").arg(maskSecretUrl(url.toString())));
    });
    m_name->setFocus();
}

QString ScriptUrlDialog::scriptName() const
{
    return m_name->text().trimmed();
}

QUrl ScriptUrlDialog::scriptUrl() const
{
    return QUrl(m_url->text().trimmed());
}

void ScriptUrlDialog::tryAccept()
{
    const QUrl url = scriptUrl();
    QString error;
    if (!ScriptDownloader::isAllowedUrl(url, &error)) {
        setErrorText(error);
        return;
    }
    if (!m_trust->isChecked()) {
        setErrorText(QStringLiteral("导入远程脚本前必须勾选来源确认"));
        return;
    }
    accept();
}

ScriptNameDialog::ScriptNameDialog(QWidget* parent, const QString& initialName)
    : MaterialDialog(parent, QStringLiteral("新建 JavaScript 脚本"),
                     QStringLiteral("脚本会先保存到本机，随后可以编辑、测试和关联订阅。"))
{
    setDialogWidth(520);
    contentLayout()->addWidget(roleLabel(QStringLiteral("脚本名称"), "bodyStrong", this));
    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(QStringLiteral("例如：统一 DNS 与规则"));
    m_name->setMaxLength(80);
    m_name->setText(initialName);
    contentLayout()->addWidget(m_name);

    auto* cancel = new M3Button(QStringLiteral("取消"), M3Button::Variant::Text, this);
    auto* create = new M3Button(QStringLiteral("创建并编辑"), M3Button::Variant::Filled, this);
    footerLayout()->addWidget(cancel);
    footerLayout()->addWidget(create);
    setPrimaryAction(create);
    connect(cancel, &QAbstractButton::clicked, this, &QDialog::reject);
    connect(create, &QAbstractButton::clicked, this, [this] {
        if (m_name->text().trimmed().isEmpty()) {
            setErrorText(QStringLiteral("请输入脚本名称"));
            return;
        }
        accept();
    });
    connect(m_name, &QLineEdit::returnPressed, this, [this] {
        if (!m_name->text().trimmed().isEmpty())
            accept();
    });
    m_name->setFocus();
    if (!initialName.isEmpty())
        m_name->selectAll();
}

QString ScriptNameDialog::scriptName() const
{
    return m_name->text().trimmed();
}

ScriptBindingsDialog::ScriptBindingsDialog(ClashController* controller, const QString& scriptId,
                                           QWidget* parent)
    : MaterialDialog(parent, QStringLiteral("关联订阅"),
                     QStringLiteral("全局脚本先执行；订阅专属脚本随后执行。"
                                    "取消勾选某个订阅只会影响该订阅。"))
    , m_controller(controller)
    , m_scriptId(scriptId)
{
    setDialogWidth(560);
    const ScriptManager* manager = controller->scriptManager();
    const auto script = manager->find(scriptId);
    if (!script) {
        setErrorText(QStringLiteral("找不到脚本"));
        return;
    }

    m_global = new QCheckBox(QStringLiteral("全局：应用到所有订阅（先执行）"), this);
    m_global->setChecked(manager->bindings().isGlobalEnabled(scriptId));
    contentLayout()->addWidget(m_global);
    contentLayout()->addWidget(new M3Divider(this));

    auto* host = new QWidget(this);
    auto* hostLayout = new QVBoxLayout(host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(6);
    const QVector<cq::ProfileMeta> profiles = controller->profileStore()->profiles();
    for (const cq::ProfileMeta& profile : profiles) {
        auto* check = new QCheckBox(profile.name, host);
        check->setChecked(manager->bindings().isEffective(scriptId, profile.id));
        check->setToolTip(profile.id);
        hostLayout->addWidget(check);
        m_profileChecks.append(check);
        m_profileIds.append(profile.id);
    }
    if (profiles.isEmpty())
        hostLayout->addWidget(roleLabel(QStringLiteral("还没有订阅配置"), "secondary", host));
    hostLayout->addStretch(1);

    // When the global switch is turned off, start from "no association" rather
    // than silently converting every inherited profile into a specific one.
    connect(m_global, &QCheckBox::toggled, this, [this](bool checked) {
        for (QCheckBox* box : std::as_const(m_profileChecks))
            box->setChecked(checked);
    });

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(host);
    scroll->setMinimumHeight(150);
    scroll->setMaximumHeight(300);
    contentLayout()->addWidget(scroll);

    auto* note = roleLabel(
        QStringLiteral("提示：勾选全局时，单独取消某个订阅会写入一条排除关联；"
                       "脚本被停用或删除时，这些关联会自动清理。"),
        "micro", this);
    contentLayout()->addWidget(note);

    auto* cancel = new M3Button(QStringLiteral("取消"), M3Button::Variant::Text, this);
    auto* save = new M3Button(QStringLiteral("保存关联"), M3Button::Variant::Filled, this);
    footerLayout()->addWidget(cancel);
    footerLayout()->addWidget(save);
    setPrimaryAction(save);
    connect(cancel, &QAbstractButton::clicked, this, &QDialog::reject);
    connect(save, &QAbstractButton::clicked, this, &ScriptBindingsDialog::applyBindings);
}

void ScriptBindingsDialog::applyBindings()
{
    ScriptManager* manager = m_controller->scriptManager();
    QString error;
    if (!manager->setGlobalBinding(m_scriptId, m_global->isChecked(), &error)) {
        setErrorText(error);
        return;
    }
    for (int i = 0; i < m_profileIds.size(); ++i) {
        if (!manager->setProfileBinding(m_scriptId, m_profileIds.at(i),
                                        m_profileChecks.at(i)->isChecked(), &error)) {
            setErrorText(error);
            return;
        }
    }
    accept();
}

ScriptHistoryDialog::ScriptHistoryDialog(ScriptManager* manager, const QString& scriptId,
                                         QWidget* parent)
    : MaterialDialog(parent, QStringLiteral("脚本历史版本"),
                     QStringLiteral("每次保存和远程更新都会生成一个本地版本；回滚会创建新的当前版本。"))
    , m_manager(manager)
    , m_scriptId(scriptId)
{
    setDialogWidth(780);

    auto* split = new QHBoxLayout;
    split->setContentsMargins(0, 0, 0, 0);
    split->setSpacing(12);
    m_list = new QListWidget(this);
    m_list->setMinimumWidth(270);
    m_list->setMinimumHeight(280);
    split->addWidget(m_list, 0);
    m_preview = readOnlyCode(this);
    split->addWidget(m_preview, 1);
    contentLayout()->addLayout(split);
    m_details = roleLabel(QString(), "micro", this);
    contentLayout()->addWidget(m_details);
    m_feedback = roleLabel(QString(), "micro", this);
    contentLayout()->addWidget(m_feedback);

    auto* close = new M3Button(QStringLiteral("关闭"), M3Button::Variant::Text, this);
    auto* rollback = new M3Button(QStringLiteral("回滚到此版本"), M3Button::Variant::Tonal, this);
    footerLayout()->addWidget(close);
    footerLayout()->addWidget(rollback);
    setPrimaryAction(rollback);
    connect(close, &QAbstractButton::clicked, this, &QDialog::accept);
    connect(m_list, &QListWidget::currentRowChanged, this,
            [this](int) { showSelected(); });
    connect(rollback, &QAbstractButton::clicked, this, &ScriptHistoryDialog::rollbackSelected);

    rebuild();
}

void ScriptHistoryDialog::rebuild()
{
    m_list->clear();
    const auto script = m_manager->find(m_scriptId);
    if (!script) {
        setErrorText(QStringLiteral("找不到脚本"));
        return;
    }
    for (const ScriptVersion& version : script->versions) {
        const QString text =
            QStringLiteral("%1%2 · %3 · %4")
                .arg(version.current ? QStringLiteral("[当前] ") : QString(),
                     formatTime(version.savedAt), version.sha256.left(12),
                     version.note.isEmpty() ? QStringLiteral("保存") : version.note);
        auto* item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, version.id);
        item->setToolTip(version.sha256);
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
    else
        m_feedback->setText(QStringLiteral("还没有历史版本"));
}

void ScriptHistoryDialog::showSelected()
{
    const QListWidgetItem* item = m_list->currentItem();
    if (!item)
        return;
    const QString versionId = item->data(Qt::UserRole).toString();
    QString error;
    const QByteArray content = m_manager->versionContent(m_scriptId, versionId, &error);
    if (!error.isEmpty()) {
        m_preview->setPlainText(error);
        return;
    }
    m_preview->setPlainText(QString::fromUtf8(content));
    const auto script = m_manager->find(m_scriptId);
    if (script) {
        for (const ScriptVersion& version : script->versions) {
            if (version.id == versionId) {
                m_details->setText(QStringLiteral("版本 %1 · %2 · %3 字节 · 保存于 %4")
                                       .arg(version.id, version.sha256,
                                            QString::number(version.size),
                                            formatTime(version.savedAt)));
                break;
            }
        }
    }
}

void ScriptHistoryDialog::rollbackSelected()
{
    const QListWidgetItem* item = m_list->currentItem();
    if (!item)
        return;
    const auto script = m_manager->find(m_scriptId);
    if (!script)
        return;
    const QString versionId = item->data(Qt::UserRole).toString();
    bool current = false;
    for (const ScriptVersion& version : script->versions) {
        if (version.id == versionId && version.current)
            current = true;
    }
    if (current) {
        m_feedback->setText(QStringLiteral("这个版本已经是当前版本"));
        return;
    }
    if (!ConfirmDialog::ask(this, QStringLiteral("回滚脚本版本"),
                            QStringLiteral("将把选中的历史内容保存为新的当前版本。"
                                           "此操作不会删除历史记录。"),
                            QStringLiteral("回滚"), false)) {
        return;
    }
    QString error;
    if (!m_manager->rollback(m_scriptId, versionId, &error)) {
        m_feedback->setText(error);
        m_feedback->setStyleSheet(QStringLiteral("color: %1;").arg(pal().error.name()));
        return;
    }
    m_feedback->setText(QStringLiteral("已回滚，当前配置将重新生成"));
    m_feedback->setStyleSheet(QStringLiteral("color: %1;").arg(pal().primary.name()));
    rebuild();
}

ScriptLogsDialog::ScriptLogsDialog(ScriptManager* manager, const QString& scriptId,
                                   QWidget* parent)
    : MaterialDialog(parent, QStringLiteral("脚本运行日志"),
                     QStringLiteral("记录脚本执行、远程更新和失败原因。日志只保存在本机。"))
    , m_manager(manager)
    , m_scriptId(scriptId)
{
    setDialogWidth(820);
    m_view = readOnlyCode(this);
    m_view->setMinimumHeight(360);
    contentLayout()->addWidget(m_view);

    auto* close = new M3Button(QStringLiteral("关闭"), M3Button::Variant::Text, this);
    auto* clear = new M3Button(QStringLiteral("清空日志"), M3Button::Variant::Tonal, this);
    auto* refreshButton = new M3Button(QStringLiteral("刷新"), M3Button::Variant::Text, this);
    footerLayout()->addWidget(clear);
    footerLayout()->addWidget(refreshButton);
    footerLayout()->addWidget(close);
    connect(close, &QAbstractButton::clicked, this, &QDialog::accept);
    connect(clear, &QAbstractButton::clicked, this, [this] {
        m_manager->clearLogs();
        refresh();
    });
    connect(refreshButton, &QAbstractButton::clicked, this, &ScriptLogsDialog::refresh);
    refresh();
}

void ScriptLogsDialog::refresh()
{
    const QVector<ScriptLogEntry> entries =
        m_scriptId.isEmpty() ? m_manager->logs().entries()
                             : m_manager->logs().entriesFor(m_scriptId);
    QStringList lines;
    for (const ScriptLogEntry& entry : entries)
        lines.append(logLine(entry));
    m_view->setPlainText(lines.isEmpty() ? QStringLiteral("暂无日志") : lines.join(QLatin1Char('\n')));
    m_view->moveCursor(QTextCursor::End);
}

ScriptSettingsDialog::ScriptSettingsDialog(ScriptManager* manager, QWidget* parent)
    : MaterialDialog(parent, QStringLiteral("脚本执行设置"),
                     QStringLiteral("限制来自不受信任脚本的执行时间与内存。默认自动更新关闭。"))
    , m_manager(manager)
{
    setDialogWidth(560);
    const ScriptSettings settings = manager->settings();

    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(10);
    m_timeout = new QSpinBox(this);
    m_timeout->setRange(100, 120000);
    m_timeout->setSuffix(QStringLiteral(" ms"));
    m_timeout->setValue(settings.timeoutMs);
    form->addRow(QStringLiteral("单次执行超时"), m_timeout);
    m_memory = new QSpinBox(this);
    m_memory->setRange(8, 2048);
    m_memory->setSuffix(QStringLiteral(" MB"));
    m_memory->setValue(static_cast<int>(settings.memoryLimitBytes / (1024 * 1024)));
    form->addRow(QStringLiteral("内存上限"), m_memory);
    m_logRetention = new QSpinBox(this);
    m_logRetention->setRange(100, 10000);
    m_logRetention->setValue(settings.logRetention);
    form->addRow(QStringLiteral("内存日志条数"), m_logRetention);
    contentLayout()->addLayout(form);

    m_stopOnError = new QCheckBox(
        QStringLiteral("脚本失败时终止本次配置生成（推荐）"), this);
    m_stopOnError->setChecked(settings.stopOnError);
    contentLayout()->addWidget(m_stopOnError);
    m_autoUpdate = new QCheckBox(
        QStringLiteral("允许远程脚本按各自间隔自动检查并更新（默认关闭）"), this);
    m_autoUpdate->setChecked(settings.autoUpdateEnabled);
    contentLayout()->addWidget(m_autoUpdate);

    auto* note = roleLabel(
        QStringLiteral("即使自动更新开启，每个远程脚本还需要单独开启自动更新。"
                       "手动检查更新始终可用。"),
        "micro", this);
    contentLayout()->addWidget(note);

    auto* cancel = new M3Button(QStringLiteral("取消"), M3Button::Variant::Text, this);
    auto* save = new M3Button(QStringLiteral("保存"), M3Button::Variant::Filled, this);
    footerLayout()->addWidget(cancel);
    footerLayout()->addWidget(save);
    setPrimaryAction(save);
    connect(cancel, &QAbstractButton::clicked, this, &QDialog::reject);
    connect(save, &QAbstractButton::clicked, this, &ScriptSettingsDialog::save);
}

void ScriptSettingsDialog::save()
{
    ScriptSettings settings;
    settings.timeoutMs = m_timeout->value();
    settings.memoryLimitBytes = static_cast<qint64>(m_memory->value()) * 1024 * 1024;
    settings.stopOnError = m_stopOnError->isChecked();
    settings.autoUpdateEnabled = m_autoUpdate->isChecked();
    settings.logRetention = m_logRetention->value();
    QString error;
    if (!m_manager->updateSettings(settings, &error)) {
        setErrorText(error);
        return;
    }
    accept();
}

ScriptTestDialog::ScriptTestDialog(ClashController* controller, const QString& scriptName,
                                   const QByteArray& source, QWidget* parent)
    : MaterialDialog(parent, QStringLiteral("测试 JavaScript 脚本"),
                     QStringLiteral("在后台 QuickJS 沙箱中执行当前编辑器内容，并查看配置差异。"))
    , m_controller(controller)
    , m_scriptName(scriptName)
{
    ScriptStep step;
    step.scriptId = QStringLiteral("preview");
    step.name = scriptName;
    step.source = QString::fromUtf8(source);
    m_steps.append(step);
    setup(m_steps, scriptName);
}

ScriptTestDialog::ScriptTestDialog(ClashController* controller,
                                   const QVector<ScriptStep>& steps, const QString& title,
                                   QWidget* parent)
    : MaterialDialog(parent, title,
                     QStringLiteral("按实际绑定顺序执行全部脚本，预览最终配置与差异；"
                                    "不会提交配置，也不会修改订阅。"))
    , m_controller(controller)
    , m_scriptName(title)
    , m_steps(steps)
{
    setup(m_steps, title);
}

ScriptTestDialog::~ScriptTestDialog()
{
    if (m_pipeline)
        m_pipeline->shutdown();
}

void ScriptTestDialog::setup(const QVector<ScriptStep>& steps, const QString& title)
{
    setDialogWidth(980);
    setSubtitle(QStringLiteral("%1 · 共 %2 个脚本").arg(title).arg(steps.size()));

    auto* top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(8);
    auto* profileLabel = roleLabel(QStringLiteral("测试订阅"), "bodyStrong", this);
    profileLabel->setWordWrap(false);
    profileLabel->setMinimumWidth(72);
    top->addWidget(profileLabel);
    m_profiles = new QComboBox(this);
    const QVector<cq::ProfileMeta> profiles = m_controller->profileStore()->profiles();
    for (const cq::ProfileMeta& profile : profiles)
        m_profiles->addItem(profile.name, profile.id);
    const QString activeId = m_controller->profileStore()->activeId();
    for (int i = 0; i < m_profiles->count(); ++i) {
        if (m_profiles->itemData(i).toString() == activeId) {
            m_profiles->setCurrentIndex(i);
            break;
        }
    }
    top->addWidget(m_profiles, 1);
    m_run = new M3Button(QStringLiteral("执行测试"), M3Button::Variant::Filled, this);
    m_run->setIconText(QStringLiteral("play_arrow"));
    top->addWidget(m_run);
    contentLayout()->addLayout(top);
    m_status = roleLabel(QString(), "micro", this);
    contentLayout()->addWidget(m_status);

    m_tabs = new QTabWidget(this);
    m_tabs->setMinimumHeight(420);

    m_diff = new QTableWidget(this);
    m_diff->setColumnCount(3);
    m_diff->setHorizontalHeaderLabels(
        {QStringLiteral("字段"), QStringLiteral("执行前"), QStringLiteral("执行后")});
    m_diff->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_diff->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_diff->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_diff->verticalHeader()->setVisible(false);
    m_diff->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_diff->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tabs->addTab(m_diff, QStringLiteral("修改差异"));

    m_before = readOnlyCode(this);
    m_tabs->addTab(m_before, QStringLiteral("执行前"));
    m_after = readOnlyCode(this);
    m_tabs->addTab(m_after, QStringLiteral("执行后"));
    m_logs = readOnlyCode(this);
    m_tabs->addTab(m_logs, QStringLiteral("console 日志"));
    m_error = readOnlyCode(this);
    m_tabs->addTab(m_error, QStringLiteral("错误与堆栈"));
    contentLayout()->addWidget(m_tabs);

    auto* close = new M3Button(QStringLiteral("关闭"), M3Button::Variant::Text, this);
    footerLayout()->addWidget(close);
    connect(close, &QAbstractButton::clicked, this, &QDialog::accept);

    m_pipeline = new ScriptPipeline(this);
    connect(m_pipeline, &ScriptPipeline::finished, this, &ScriptTestDialog::handleResult);
    connect(m_run, &QAbstractButton::clicked, this, &ScriptTestDialog::run);
    if (m_profiles->count() == 0)
        showError(QStringLiteral("没有可用的订阅配置，无法测试脚本"));
}

void ScriptTestDialog::showError(const QString& message)
{
    m_status->setText(message);
    m_status->setStyleSheet(QStringLiteral("color: %1;").arg(pal().error.name()));
    m_error->setPlainText(message);
}

void ScriptTestDialog::run()
{
    if (!m_pipeline || m_pipeline->isBusy())
        return;
    const QString profileId = m_profiles->currentData().toString();
    if (profileId.isEmpty()) {
        showError(QStringLiteral("请选择一个订阅"));
        return;
    }
    QString error;
    const QByteArray raw = m_controller->profileStore()->configContent(profileId, &error);
    if (!error.isEmpty() || raw.isEmpty()) {
        showError(error.isEmpty() ? QStringLiteral("订阅配置为空") : error);
        return;
    }

    ScriptPipeline::Request request;
    request.requestId = QStringLiteral("preview");
    request.profileId = profileId;
    request.profileName = m_profiles->currentText();
    request.rawYaml = QString::fromUtf8(raw);
    request.settings = m_controller->settings();
    request.scriptSettings = m_controller->scriptManager()->settings();
    request.stopOnError = true;
    request.steps = m_steps;

    m_status->setText(QStringLiteral("正在后台执行…"));
    m_status->setStyleSheet(QStringLiteral("color: %1;").arg(pal().onSurfaceVariant.name()));
    m_run->setEnabled(false);
    m_diff->setRowCount(0);
    m_before->clear();
    m_after->clear();
    m_logs->clear();
    m_error->clear();
    if (!m_pipeline->run(request))
        showError(QStringLiteral("已有测试正在执行"));
}

void ScriptTestDialog::handleResult(const ScriptPipelineResult& result)
{
    m_run->setEnabled(true);
    m_before->setPlainText(result.baselineYaml);
    m_after->setPlainText(result.finalYaml.isEmpty()
                              ? QStringLiteral("脚本失败，未生成最终配置")
                              : result.finalYaml);

    QStringList logs;
    for (const ScriptStepResult& step : result.steps) {
        for (const ScriptLogLine& line : step.logs)
            logs.append(QStringLiteral("[%1] %2").arg(logLevelLabel(line.level), line.message));
        if (!step.errorMessage.isEmpty()) {
            logs.append(QStringLiteral("[错误] %1（第 %2 行）")
                            .arg(step.errorMessage)
                            .arg(step.line));
        }
    }
    m_logs->setPlainText(logs.join(QLatin1Char('\n')));

    if (!result.steps.isEmpty() && !result.steps.first().stack.isEmpty())
        m_error->setPlainText(result.steps.first().stack);
    else if (!result.error.isEmpty())
        m_error->setPlainText(result.error);

    m_diff->setRowCount(0);
    for (const nq::yaml::DiffEntry& entry : result.diff.entries) {
        const int row = m_diff->rowCount();
        m_diff->insertRow(row);
        QString path = entry.path;
        if (entry.kind == nq::yaml::DiffEntry::Kind::Added)
            path += QStringLiteral("（新增）");
        else if (entry.kind == nq::yaml::DiffEntry::Kind::Removed)
            path += QStringLiteral("（删除）");
        m_diff->setItem(row, 0, new QTableWidgetItem(path));
        m_diff->setItem(row, 1, new QTableWidgetItem(
                                     entry.kind == nq::yaml::DiffEntry::Kind::Added
                                         ? QStringLiteral("—")
                                         : entry.before));
        m_diff->setItem(row, 2, new QTableWidgetItem(
                                     entry.kind == nq::yaml::DiffEntry::Kind::Removed
                                         ? QStringLiteral("—")
                                         : entry.after));
    }

    if (result.ok) {
        m_status->setText(QStringLiteral("执行成功 · 修改 %1 处 · 耗时 %2 ms")
                              .arg(result.diff.total)
                              .arg(result.durationMs));
        m_status->setStyleSheet(QStringLiteral("color: %1;").arg(pal().success.name()));
        m_tabs->setCurrentWidget(m_diff);
    } else {
        m_status->setText(QStringLiteral("执行失败 · %1").arg(result.error));
        m_status->setStyleSheet(QStringLiteral("color: %1;").arg(pal().error.name()));
        m_tabs->setCurrentWidget(m_error);
    }
}

ScriptUpdateDiffDialog::ScriptUpdateDiffDialog(const ScriptUpdateResult& update, QWidget* parent)
    : MaterialDialog(parent, QStringLiteral("远程脚本更新差异"),
                     QStringLiteral("左侧是更新前的缓存，右侧是刚下载并通过校验的新内容。"))
{
    setDialogWidth(1000);

    auto* split = new QHBoxLayout;
    split->setContentsMargins(0, 0, 0, 0);
    split->setSpacing(12);
    auto* left = new QVBoxLayout;
    left->addWidget(roleLabel(QStringLiteral("更新前 · %1").arg(update.oldSha256.left(12)),
                              "bodyStrong", this));
    auto* oldView = readOnlyCode(this);
    oldView->setPlainText(QString::fromUtf8(update.oldContent));
    left->addWidget(oldView);
    split->addLayout(left, 1);

    auto* right = new QVBoxLayout;
    right->addWidget(roleLabel(QStringLiteral("更新后 · %1").arg(update.newSha256.left(12)),
                               "bodyStrong", this));
    auto* newView = readOnlyCode(this);
    newView->setPlainText(QString::fromUtf8(update.newContent));
    right->addWidget(newView);
    split->addLayout(right, 1);
    contentLayout()->addLayout(split);

    auto* note = roleLabel(
        QStringLiteral("SHA-256 只用于完整性与变化检测，不能单独证明远程来源可信。"),
        "micro", this);
    contentLayout()->addWidget(note);

    auto* close = new M3Button(QStringLiteral("关闭"), M3Button::Variant::Filled, this);
    footerLayout()->addWidget(close);
    setPrimaryAction(close);
    connect(close, &QAbstractButton::clicked, this, &QDialog::accept);
}
