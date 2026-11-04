#pragma once

#include <QDialog>

class QHBoxLayout;
class QLabel;
class QVBoxLayout;
class M3Button;
class M3Card;
class M3IconButton;

class MaterialDialog : public QDialog {
    Q_OBJECT

public:
    explicit MaterialDialog(QWidget* parent, const QString& title,
                            const QString& subtitle = {});
    ~MaterialDialog() override;

    QVBoxLayout* contentLayout() const { return m_contentLayout; }
    QHBoxLayout* footerLayout() const { return m_footerLayout; }
    void setDialogWidth(int width);
    void setPrimaryAction(M3Button* button);
    void setSubtitle(const QString& text);
    void setErrorText(const QString& text);
    void clearError();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    virtual void requestClose();

private:
    void positionOverParent();

    M3Card* m_surface = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QLabel* m_errorLabel = nullptr;
    M3IconButton* m_close = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
    QHBoxLayout* m_footerLayout = nullptr;
    M3Button* m_primary = nullptr;
    int m_width = 500;
};
