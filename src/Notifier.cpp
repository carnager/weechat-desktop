#include "Notifier.h"

#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QSystemTrayIcon>

#ifdef HAVE_DBUS
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#endif

namespace {
const QString kService = QStringLiteral("org.freedesktop.Notifications");
const QString kPath = QStringLiteral("/org/freedesktop/Notifications");

QIcon badgeIcon(int unread, int highlights)
{
    QIcon base = QIcon(QStringLiteral(":/resources/icon.svg"));
    if (unread <= 0 && highlights <= 0)
        return base;
    QPixmap pm = base.pixmap(64, 64);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(highlights > 0 ? QColor(QStringLiteral("#ef5b6b")) : QColor(QStringLiteral("#8ea2ff")));
    p.drawEllipse(QRectF(36, 0, 28, 28));
    return QIcon(pm);
}
} // namespace

Notifier::Notifier(QObject *parent)
    : QObject(parent)
{
#ifdef HAVE_DBUS
    auto bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        m_dbus = true;
        bus.connect(kService, kPath, kService, QStringLiteral("ActionInvoked"), this,
                    SLOT(onActionInvoked(uint, QString)));
        bus.connect(kService, kPath, kService, QStringLiteral("NotificationClosed"), this,
                    SLOT(onClosed(uint, uint)));
    }
#endif
}

Notifier::~Notifier() = default;

bool Notifier::trayAvailable() const { return QSystemTrayIcon::isSystemTrayAvailable(); }

void Notifier::setTrayVisible(bool visible)
{
    if (!visible) {
        if (m_tray)
            m_tray->hide();
        return;
    }
    if (!trayAvailable())
        return;
    if (!m_tray) {
        m_tray = new QSystemTrayIcon(badgeIcon(0, 0), this);
        m_tray->setToolTip(QStringLiteral("WeeChat Desktop"));
        m_menu = new QMenu();
        m_menu->addAction(tr("Show / Hide"), this, &Notifier::toggleWindowRequested);
        m_menu->addSeparator();
        m_menu->addAction(tr("Quit"), this, &Notifier::quitRequested);
        m_tray->setContextMenu(m_menu);
        connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
            if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick)
                emit toggleWindowRequested();
        });
        connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this] { emit activated(QString()); });
    }
    m_tray->show();
}

void Notifier::setUnread(int unread, int highlights)
{
    if (!m_tray)
        return;
    m_tray->setIcon(badgeIcon(unread, highlights));
    m_tray->setToolTip(unread > 0 ? tr("WeeChat Desktop — %n unread", nullptr, unread) : QStringLiteral("WeeChat Desktop"));
}

void Notifier::notify(const QString &key, const QString &title, const QString &body)
{
#ifdef HAVE_DBUS
    if (m_dbus) {
        QDBusInterface iface(kService, kPath, kService, QDBusConnection::sessionBus());
        const uint replaces = m_lastForKey.value(key, 0);
        QVariantMap hints{{QStringLiteral("desktop-entry"), QStringLiteral("weechat-desktop")},
                          {QStringLiteral("category"), QStringLiteral("im.received")},
                          {QStringLiteral("urgency"), QVariant::fromValue<uchar>(1)}};
        const QStringList actions{QStringLiteral("default"), tr("Open")};
        QDBusReply<uint> reply = iface.call(QStringLiteral("Notify"), QStringLiteral("WeeChat Desktop"), replaces,
                                            QStringLiteral("weechat-desktop"), title, body.toHtmlEscaped(),
                                            actions, hints, -1);
        if (reply.isValid()) {
            m_keys.remove(replaces);
            m_keys.insert(reply.value(), key);
            m_lastForKey.insert(key, reply.value());
            return;
        }
    }
#endif
    if (m_tray && m_tray->isVisible())
        m_tray->showMessage(title, body, QSystemTrayIcon::Information, 6000);
}

void Notifier::withdraw(const QString &key)
{
#ifdef HAVE_DBUS
    const uint id = m_lastForKey.take(key);
    if (m_dbus && id) {
        m_keys.remove(id);
        QDBusInterface iface(kService, kPath, kService, QDBusConnection::sessionBus());
        iface.asyncCall(QStringLiteral("CloseNotification"), id);
    }
#else
    Q_UNUSED(key);
#endif
}

void Notifier::onActionInvoked(uint id, const QString &action)
{
    Q_UNUSED(action);
    if (m_keys.contains(id))
        emit activated(m_keys.value(id));
}

void Notifier::onClosed(uint id, uint reason)
{
    Q_UNUSED(reason);
    const QString key = m_keys.take(id);
    if (!key.isEmpty() && m_lastForKey.value(key) == id)
        m_lastForKey.remove(key);
}
