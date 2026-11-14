#pragma once

#include <QHash>
#include <QObject>

class QSystemTrayIcon;
class QMenu;

/// Desktop notifications (freedesktop D-Bus, with click-to-open) and the tray icon.
class Notifier : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT)
public:
    explicit Notifier(QObject *parent = nullptr);
    ~Notifier() override;

    bool trayAvailable() const;
    void setTrayVisible(bool visible);

    /// Show a notification; [key] comes back through activated() when it's clicked.
    void notify(const QString &key, const QString &title, const QString &body);
    /// Withdraw notifications for [key] (the buffer was read).
    void withdraw(const QString &key);
    void setUnread(int unread, int highlights);

signals:
    void activated(const QString &key);
    void toggleWindowRequested();
    void quitRequested();

private slots:
    void onActionInvoked(uint id, const QString &action);
    void onClosed(uint id, uint reason);

private:
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QHash<uint, QString> m_keys;          // D-Bus notification id → key
    QHash<QString, uint> m_lastForKey;    // key → last notification id (replaced, not stacked)
    bool m_dbus = false;
};
