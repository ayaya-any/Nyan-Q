#include "ui/dialogs/ProfileDialog.h"

#include "app/Fonts.h"
#include "ui/widgets/MaterialWidgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QLabel* fieldLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "bodyStrong");
    return label;
}
}  // namespace

ProfileDialog::ProfileDialog(QWidget* parent)
    : MaterialDialog(parent, QStringLiteral("导入订阅"),
                     QStringLiteral("添加 Clash / Mihomo 格式的远程配置。"))
{
    setDialogWidth(520);

    auto* intro = new QLabel(
        QStringLiteral("粘贴机场提供的 Clash 订阅链接。导入成功后会自动设为当前配置，"
                       "并在连接时由 Mihomo 读取。"),
        this);
    intro->setProperty("role", "secondary");
    intro->setWordWrap(true);
    contentLayout()->addWidget(intro);
    contentLayout()->addSpacing(6);

    contentLayout()->addWidget(fieldLabel(QStringLiteral("配置名称（可选）"), this));
    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(QStringLiteral("例如：Aurora · 流媒体"));
    m_name->setMaxLength(80);
    contentLayout()->addWidget(m_name);

    contentLayout()->addSpacing(4);
    contentLayout()->addWidget(fieldLabel(QStringLiteral("订阅地址"), this));
    m_url = new QLineEdit(this);
    m_url->setPlaceholderText(QStringLiteral("https://example.com/api/v1/client/subscribe?token=…"));
    m_url->setClearButtonEnabled(true);
    contentLayout()->addWidget(m_url);

    auto* hint = new QLabel(
        QStringLiteral("支持 Clash YAML 订阅。仅适用于 Base64 节点列表的链接无法直接使用。"),
        this);
    hint->setProperty("role", "micro");
    hint->setWordWrap(true);
    contentLayout()->addWidget(hint);

    auto* cancel = new M3Button(QStringLiteral("取消"), M3Button::Variant::Text, this);
    auto* import = new M3Button(QStringLiteral("导入订阅"), M3Button::Variant::Filled, this);
    import->setIconText(QStringLiteral("download"));
    footerLayout()->addWidget(cancel);
    footerLayout()->addWidget(import);
    setPrimaryAction(import);

    connect(cancel, &QAbstractButton::clicked, this, &QDialog::reject);
    connect(import, &QAbstractButton::clicked, this, &ProfileDialog::tryAccept);
    connect(m_url, &QLineEdit::returnPressed, this, &ProfileDialog::tryAccept);
    connect(m_name, &QLineEdit::returnPressed, this, &ProfileDialog::tryAccept);
    connect(m_url, &QLineEdit::textChanged, this, [this](const QString& text) {
        clearError();
        Q_UNUSED(text)
    });

    m_name->setFocus();
}

QString ProfileDialog::profileName() const
{
    return m_name->text().trimmed();
}

QUrl ProfileDialog::profileUrl() const
{
    return QUrl(m_url->text().trimmed());
}

void ProfileDialog::validate(const QString& text)
{
    const QUrl url(text.trimmed());
    if (text.trimmed().isEmpty()) {
        setErrorText(QStringLiteral("请填写订阅地址"));
        return;
    }
    if (!url.isValid() || (url.scheme() != QStringLiteral("http") &&
                           url.scheme() != QStringLiteral("https"))) {
        setErrorText(QStringLiteral("订阅地址必须是有效的 http/https URL"));
        return;
    }
    clearError();
}

void ProfileDialog::tryAccept()
{
    validate(m_url->text());
    const QUrl url = profileUrl();
    if (url.isValid() &&
        (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https")))
        accept();
}
