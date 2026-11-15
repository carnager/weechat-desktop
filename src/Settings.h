#pragma once

#include <QObject>
#include <QSettings>
#include <QVariant>

#define WD_SETTING(Type, name, Name, def)                                                            \
    Q_PROPERTY(Type name READ name WRITE set##Name NOTIFY name##Changed)                             \
public:                                                                                              \
    Type name() const { return m_s.value(QStringLiteral(#name), QVariant::fromValue<Type>(def)).template value<Type>(); } \
    void set##Name(const Type &v)                                                                    \
    {                                                                                                \
        if (name() == v && m_s.contains(QStringLiteral(#name)))                                      \
            return;                                                                                  \
        m_s.setValue(QStringLiteral(#name), QVariant::fromValue<Type>(v));                           \
        emit name##Changed();                                                                        \
        emit changed();                                                                              \
    }                                                                                                \
    Q_SIGNAL void name##Changed();                                                                   \
                                                                                                     \
private:

/// Persistent preferences, exposed to QML as the `AppSettings` singleton.
class Settings : public QObject
{
    Q_OBJECT

    // Connection
    WD_SETTING(QString, host, Host, QString())
    WD_SETTING(int, port, Port, 9000)
    WD_SETTING(bool, tls, Tls, true)
    WD_SETTING(QString, password, Password, QString())
    WD_SETTING(QString, pinnedSha256, PinnedSha256, QString())
    WD_SETTING(bool, autoConnect, AutoConnect, true)

    // Appearance
    WD_SETTING(QString, theme, Theme, QStringLiteral("system")) // system | dark | light
    WD_SETTING(QString, accent, Accent, QStringLiteral("#8ea2ff"))
    WD_SETTING(int, fontSize, FontSize, 14)
    WD_SETTING(QString, layout, Layout, QStringLiteral("cozy")) // cozy | compact
    WD_SETTING(bool, use24h, Use24h, true)
    WD_SETTING(bool, renderColors, RenderColors, true)
    WD_SETTING(bool, showJoinPart, ShowJoinPart, true)
    WD_SETTING(bool, hideSpecialBuffers, HideSpecialBuffers, false)

    // Messages
    WD_SETTING(bool, emojiShortcodes, EmojiShortcodes, true)
    WD_SETTING(bool, linkPreviews, LinkPreviews, false) // off by default: fetching tells sites your IP
    WD_SETTING(bool, inlineImages, InlineImages, false)
    // "/np": the MPRIS player to ask (NowPlaying::keyOf), empty for none, and what to send.
    WD_SETTING(QString, npPlayer, NpPlayer, QString())
    WD_SETTING(QString, npFormat, NpFormat, QStringLiteral("/me np: {track}"))

    // Behaviour
    WD_SETTING(bool, notifications, Notifications, true)
    WD_SETTING(bool, notifyWhenFocused, NotifyWhenFocused, false)
    // Nicks whose highlights don't count: no mention, badge or notification. Space separated, * wildcards.
    WD_SETTING(QString, mutedNicks, MutedNicks,
               QStringLiteral("NickServ ChanServ MemoServ OperServ HostServ BotServ SaslServ Global Alis *status"))
    WD_SETTING(bool, closeToTray, CloseToTray, false)
    WD_SETTING(bool, showNicklist, ShowNicklist, true)
    WD_SETTING(bool, showSidebar, ShowSidebar, true)
    WD_SETTING(int, sidebarWidth, SidebarWidth, 250)
    WD_SETTING(int, nicklistWidth, NicklistWidth, 210)
    WD_SETTING(QString, lastBuffer, LastBuffer, QString())

public:
    explicit Settings(QObject *parent = nullptr);
    static Settings *instance();

    Q_INVOKABLE bool hasConnection() const { return !host().isEmpty(); }

signals:
    void changed();

private:
    mutable QSettings m_s;
};

#undef WD_SETTING
