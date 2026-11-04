#pragma once

#include <QObject>
#include <QString>
#include <QVariant>

class SystemProxy final : public QObject {
    Q_OBJECT

public:
    explicit SystemProxy(QObject* parent = nullptr);
    ~SystemProxy() override;

    bool supported() const;
    bool enable(int port, QString* error = nullptr);
    bool disable(QString* error = nullptr);
    bool isEnabled() const;
    bool appliedByUs() const { return m_applied; }
    int port() const { return m_port; }

    // Restores the values that existed before Clash-Q first changed them.
    void restoreOriginal();

signals:
    void stateChanged(bool enabled);

private:
    void captureOriginal();
    bool applyRegistry(bool enabled, const QString& server, const QString& bypass, QString* error);
    static void notifySystem();

    bool m_captured = false;
    bool m_applied = false;
    int m_port = 0;
    QVariant m_originalEnable;
    QVariant m_originalServer;
    QVariant m_originalOverride;
    bool m_hadEnable = false;
    bool m_hadServer = false;
    bool m_hadOverride = false;
};
