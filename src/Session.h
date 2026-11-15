#pragma once

#include "BufferListModel.h"
#include "LineModel.h"
#include "RelayConnection.h"

#include <QHash>
#include <QObject>
#include <QRegularExpression>
#include <QPointer>
#include <QTimer>
#include <QSet>
#include <QVariantList>

class Buffer;
class Settings;
class Notifier;
class Completer;
class NowPlaying;

/// The relay session as the UI sees it: connection state, the buffers, the buffer being shown,
/// and everything you can do (send, switch, mark read, …). Exposed to QML as `Relay`.
class Session : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool synced READ synced NOTIFY syncedChanged)
    Q_PROPERTY(QString weechatVersion READ weechatVersion NOTIFY syncedChanged)
    Q_PROPERTY(QVariantMap untrustedCertificate READ untrustedCertificate NOTIFY untrustedCertificateChanged)
    Q_PROPERTY(BufferListModel *buffers READ buffers CONSTANT)
    Q_PROPERTY(BufferFilterModel *sidebar READ sidebar CONSTANT)
    Q_PROPERTY(Buffer *current READ current NOTIFY currentChanged)
    Q_PROPERTY(int totalUnread READ totalUnread NOTIFY totalsChanged)
    Q_PROPERTY(int totalHighlights READ totalHighlights NOTIFY totalsChanged)
    Q_PROPERTY(bool windowActive READ windowActive WRITE setWindowActive NOTIFY windowActiveChanged)
    Q_PROPERTY(QVariantList mentions READ mentions NOTIFY mentionsChanged)
    /// Mentions including those from muted nicks (Settings::mutedNicks), each with "muted".
    Q_PROPERTY(QVariantList allMentions READ allMentions NOTIFY mentionsChanged)
    Q_PROPERTY(int mutedMentions READ mutedMentions NOTIFY mentionsChanged)
    Q_PROPERTY(int unseenMentions READ unseenMentions NOTIFY mentionsChanged)
    Q_PROPERTY(Completer *completer READ completer CONSTANT)
    /// The latest /whois asked for through requestWhois(): nick, server, userHost, realName,
    /// rows [{key, label, text}], loading, error.
    Q_PROPERTY(QVariantMap whois READ whois NOTIFY whoisChanged)

public:
    explicit Session(Settings *settings, Notifier *notifier, QObject *parent = nullptr);
    ~Session() override;

    int state() const { return m_conn.state(); }
    QString stateText() const;
    QString errorString() const { return m_conn.errorString(); }
    bool connected() const { return m_conn.state() == RelayConnection::Connected; }
    bool synced() const { return m_synced; }
    QString weechatVersion() const { return m_version; }
    QVariantMap untrustedCertificate() const { return m_conn.untrustedCertificate(); }
    BufferListModel *buffers() { return &m_buffers; }
    BufferFilterModel *sidebar() { return &m_sidebar; }
    Buffer *current() const { return m_current; }
    int totalUnread() const;
    int totalHighlights() const;
    bool windowActive() const { return m_windowActive; }
    void setWindowActive(bool active);
    QVariantList mentions() const { return m_mentions; }
    QVariantList allMentions() const { return m_allMentions; }
    int mutedMentions() const { return int(m_allMentions.size() - m_mentions.size()); }
    /// Whether highlights from [nick] are muted.
    Q_INVOKABLE bool isMuted(const QString &nick) const;
    /// Add [nick] to (or take it off) the muted list.
    Q_INVOKABLE void setMuted(const QString &nick, bool muted);
    int unseenMentions() const { return m_unseenMentions; }
    Q_INVOKABLE void markMentionsSeen() { m_unseenMentions = 0; emit mentionsChanged(); }
    Completer *completer() const { return m_completer; }
    NowPlaying *nowPlaying() const { return m_nowPlaying; }

    Q_INVOKABLE void connectToRelay();
    Q_INVOKABLE void disconnectFromRelay();
    Q_INVOKABLE void reconnectNow() { m_conn.reconnectNow(); }
    /// Trust the certificate the relay presented and connect again.
    Q_INVOKABLE void trustCertificate(const QString &sha256);

    Q_INVOKABLE void openBuffer(Buffer *buffer);
    Q_INVOKABLE void openBufferByName(const QString &fullName);
    Q_INVOKABLE void openBufferById(qint64 id);
    /// Alt+1…9 style: the [n]-th buffer in sidebar order (1-based).
    Q_INVOKABLE void openBufferAt(int n);
    Q_INVOKABLE void openRelative(int delta);
    /// Jump to the buffer most worth reading: highlights first, then private, then messages.
    Q_INVOKABLE bool openNextUnread();
    Q_INVOKABLE void openPrevious();
    Q_INVOKABLE Buffer *bufferByName(const QString &fullName) const;

    /// Send what the user typed. Multiple lines are sent one by one.
    Q_INVOKABLE bool sendInput(const QString &text);
    /// Run a command in [buffer] (or the current one) on the user's behalf.
    Q_INVOKABLE bool command(const QString &text, Buffer *buffer = nullptr);
    Q_INVOKABLE void loadMoreHistory();
    Q_INVOKABLE void markRead(Buffer *buffer);
    Q_INVOKABLE void markAllRead();
    Q_INVOKABLE void closeBuffer(Buffer *buffer);
    Q_INVOKABLE void openQuery(const QString &nick);
    Q_INVOKABLE void joinChannel(const QString &channel);
    Q_INVOKABLE void clearMentions();
    /// Ask the current buffer's server who [nick] is; the reply fills [whois] instead of the chat.
    Q_INVOKABLE void requestWhois(const QString &nick);
    QVariantMap whois() const { return m_whois; }

    /// Ranked buffers for the quick switcher: fuzzy match, unread and recency boosted.
    Q_INVOKABLE QVariantList switcherResults(const QString &query, int limit = 40) const;

    /// Previous/next entry of the current buffer's input history (for Up/Down in the input).
    Q_INVOKABLE QString historyUp(const QString &currentText);
    Q_INVOKABLE QString historyDown();

    /// Re-render after theme/format settings changed.
    Q_INVOKABLE void refreshFormatting(bool dark, const QString &accent, const QString &link);

signals:
    void stateChanged();
    void syncedChanged();
    void untrustedCertificateChanged();
    void currentChanged();
    void totalsChanged();
    void windowActiveChanged();
    void mentionsChanged();
    void whoisChanged();
    /// Ask the UI to bring the window up (e.g. a notification was clicked).
    void raiseRequested();
    void toast(const QString &message);

private:
    void onOpened();
    void onMessage(const QJsonObject &msg);
    bool handleEvent(const QString &event, const QJsonObject &msg);
    void onBuffers(const QJsonArray &arr);
    void onBuffer(const QJsonObject &obj, const QString &event);
    void onLine(const QJsonObject &msg);
    void onLines(const QString &bufferName, const QJsonArray &arr);
    void onHotlist(const QJsonArray &arr);
    void onNicks(const QString &bufferName, const QJsonObject &root);
    void send(const QString &path, const QJsonObject &body = {});
    void requestLines(Buffer *b, int count);
    void requestNicks(Buffer *b);
    void scheduleNickRefresh(Buffer *b);
    void maybeNotify(Buffer *b, const LineModel::Entry &e);
    void addMention(Buffer *b, qint64 lineId, const QString &nick, const QString &text, const QDateTime &when, bool live = false);
    void markReadRemote(Buffer *b);
    void applySettings();
    Buffer *bufferById(qint64 id) const { return m_byId.value(id); }

    Settings *m_settings;
    Notifier *m_notifier;
    Completer *m_completer;
    NowPlaying *m_nowPlaying;
    RelayConnection m_conn;
    BufferListModel m_buffers;
    BufferFilterModel m_sidebar;
    QHash<qint64, Buffer *> m_byId;
    QPointer<Buffer> m_current;
    QList<QPointer<Buffer>> m_recent; // most recently viewed first
    bool m_synced = false;
    bool m_windowActive = true;
    QString m_version;
    QVariantList m_mentions;    // shown: not from muted nicks
    QVariantList m_allMentions; // everything, newest first
    QList<QRegularExpression> m_muted;
    void updateMuted();
    void filterMentions();
    int m_unseenMentions = 0;
    QSet<QString> m_mentionKeys; // "bufferId:lineId", so history and live lines don't double up
    void collectMentions(Buffer *b);
    QSet<qint64> m_pendingNicks;
    QTimer m_nickTimer;
    QTimer m_buffersTimer;
    int m_historyPos = -1;
    QString m_historyStash;
    QHash<qint64, QVector<QJsonObject>> m_pendingLines; // lines for buffers we don't know yet
    QString m_pendingOpen;    // a buffer we asked WeeChat to open (query/join): switch when it appears
    QTimer m_readTimer;       // coalesces "mark read" while lines stream into the visible buffer
    QTimer m_hotlistTimer;    // periodic hotlist resync (reads done in other clients)
    bool m_markerArmed = false; // next line in the current buffer while unfocused gets the marker
    QHash<qint64, int> m_markerOnLoad;
    QVariantMap m_whois;
    QTimer m_whoisTimer;      // gives up on a whois the server never finishes
    bool takeWhoisLine(Buffer *b, const Protocol::Line &line); // unread count to place the marker at once history arrives
};
