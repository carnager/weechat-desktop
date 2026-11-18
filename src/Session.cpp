#include "Session.h"

#include "Buffer.h"
#include "Completer.h"
#include "EmojiStore.h"
#include "Formatter.h"
#include "Notifier.h"
#include "NowPlaying.h"
#include "Settings.h"

#include <QJsonArray>
#include <QRegularExpression>

using namespace Protocol;

namespace {
constexpr int kInitialLines = 60;   // per buffer, in the first buffer list
constexpr int kOpenLines = 300;     // fetched when a buffer is first opened
constexpr int kMoreLines = 500;     // added per "load more"
constexpr int kMaxMentions = 200;
constexpr int kMaxInputHistory = 200;

QString linesPath(qint64 id, int count)
{
    return QStringLiteral("GET /api/buffers/%1/lines?lines=-%2&colors=ansi").arg(id).arg(count);
}

/// Fuzzy subsequence score: higher is better, -1 means no match.
int fuzzyScore(const QString &hay, const QString &needle)
{
    if (needle.isEmpty())
        return 0;
    const QString h = hay.toLower(), n = needle.toLower();
    if (h == n) return 1000;
    if (h.startsWith(n)) return 800 - int(h.size());
    // Ignore the channel sigil: "arch" should rank "#archlinux" like a prefix match.
    if (h.size() > 1 && (h[0] == u'#' || h[0] == u'&') && h.mid(1).startsWith(n)) return 760 - int(h.size());
    if (const int i = h.indexOf(n); i >= 0) return 500 - i * 4 - int(h.size());
    int score = 300, hi = 0, last = -1;
    for (const QChar c : n) {
        hi = h.indexOf(c, hi);
        if (hi < 0) return -1;
        score -= (last >= 0 ? (hi - last - 1) * 3 : hi);
        last = hi++;
    }
    return std::max(score, 1);
}
} // namespace

Session::Session(Settings *settings, Notifier *notifier, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_notifier(notifier)
    , m_completer(new Completer(this))
    , m_nowPlaying(new NowPlaying(this))
{
    m_sidebar.setSourceModel(&m_buffers);
    m_sidebar.setHideSpecial(settings->hideSpecialBuffers());
    connect(settings, &Settings::hideSpecialBuffersChanged, this,
            [this] { m_sidebar.setHideSpecial(m_settings->hideSpecialBuffers()); });
    connect(settings, &Settings::showJoinPartChanged, this, &Session::applySettings);
    connect(settings, &Settings::renderColorsChanged, this, &Session::applySettings);
    connect(settings, &Settings::emojiShortcodesChanged, this, &Session::applySettings);
    connect(settings, &Settings::use24hChanged, this, &Session::applySettings);
    connect(settings, &Settings::mutedNicksChanged, this, &Session::updateMuted);
    updateMuted();
    connect(settings, &Settings::noPreviewNicksChanged, this, &Session::updatePreviewless);
    updatePreviewless();
    {
        auto &ctx = FormatContext::current();
        ctx.showJoinPart = settings->showJoinPart();
        ctx.colors = settings->renderColors();
        ctx.emojiShortcodes = settings->emojiShortcodes();
        ctx.use24h = settings->use24h();
    }

    connect(&m_conn, &RelayConnection::stateChanged, this, [this] {
        if (m_conn.state() != RelayConnection::Connected && m_synced) {
            m_synced = false;
            emit syncedChanged();
        }
        emit stateChanged();
    });
    connect(&m_conn, &RelayConnection::opened, this, &Session::onOpened);
    connect(&m_conn, &RelayConnection::messageReceived, this, &Session::onMessage);
    connect(&m_conn, &RelayConnection::untrustedCertificateChanged, this, &Session::untrustedCertificateChanged);

    m_nickTimer.setSingleShot(true);
    m_nickTimer.setInterval(400);
    connect(&m_nickTimer, &QTimer::timeout, this, [this] {
        for (qint64 id : std::as_const(m_pendingNicks))
            if (Buffer *b = bufferById(id))
                requestNicks(b);
        m_pendingNicks.clear();
    });

    m_buffersTimer.setSingleShot(true);
    m_buffersTimer.setInterval(250);
    connect(&m_buffersTimer, &QTimer::timeout, this, [this] {
        send(QStringLiteral("GET /api/buffers?lines=0&nicks=false&colors=ansi"));
    });

    m_readTimer.setSingleShot(true);
    m_readTimer.setInterval(1500);
    connect(&m_readTimer, &QTimer::timeout, this, [this] {
        if (m_current && m_windowActive)
            markReadRemote(m_current);
    });

    m_hotlistTimer.setInterval(90'000);
    connect(&m_hotlistTimer, &QTimer::timeout, this, [this] { send(QStringLiteral("GET /api/hotlist")); });

    connect(notifier, &Notifier::activated, this, [this](const QString &key) {
        if (!key.isEmpty())
            openBufferByName(key);
        emit raiseRequested();
    });
}

Session::~Session() = default;

QString Session::stateText() const
{
    switch (m_conn.state()) {
    case RelayConnection::Disconnected: return tr("Disconnected");
    case RelayConnection::Connecting: return tr("Connecting…");
    case RelayConnection::Connected: return m_synced ? tr("Connected") : tr("Syncing…");
    case RelayConnection::Reconnecting: return tr("Reconnecting…");
    case RelayConnection::Failed: return tr("Connection failed");
    }
    return {};
}

void Session::connectToRelay()
{
    RelayConnection::Params p;
    p.host = m_settings->host();
    p.port = m_settings->port();
    p.tls = m_settings->tls();
    p.password = m_settings->password();
    p.pinnedSha256 = m_settings->pinnedSha256();
    m_conn.open(p);
}

void Session::disconnectFromRelay()
{
    m_conn.close();
    m_hotlistTimer.stop();
    m_synced = false;
    emit syncedChanged();
}

void Session::trustCertificate(const QString &sha256)
{
    m_settings->setPinnedSha256(sha256);
    connectToRelay();
}

void Session::send(const QString &path, const QJsonObject &body)
{
    m_conn.send(request(path, body));
}

void Session::onOpened()
{
    send(QStringLiteral("POST /api/sync"),
         {{QStringLiteral("sync"), true}, {QStringLiteral("nicks"), true},
          {QStringLiteral("input"), false}, {QStringLiteral("colors"), QStringLiteral("ansi")}});
    send(QStringLiteral("GET /api/version"));
    send(QStringLiteral("GET /api/buffers?lines=-%1&nicks=false&colors=ansi").arg(kInitialLines));
    send(QStringLiteral("GET /api/hotlist"));
    m_hotlistTimer.start();
}

void Session::onMessage(const QJsonObject &msg)
{
    const int code = msg.value(u"code").toInt(200);
    const QString request = msg.value(u"request").toString();
    if (code >= 400) {
        const QString err = msg.value(u"body").toObject().value(u"error").toString();
        if (request.contains(u"/completion"))
            return; // optional endpoint; Completer falls back to local completion
        emit toast(err.isEmpty() ? tr("Relay error %1").arg(code) : err);
        return;
    }

    const QString event = msg.value(u"event_name").toString();
    if (!event.isEmpty() && handleEvent(event, msg))
        return;

    const QString bodyType = msg.value(u"body_type").toString();
    const QJsonValue body = msg.value(u"body");
    if (bodyType == u"buffers") {
        onBuffers(body.toArray());
    } else if (bodyType == u"buffer") {
        onBuffer(body.toObject(), event);
    } else if (bodyType == u"lines") {
        onLines(bufferNameFromRequest(request), body.toArray());
    } else if (bodyType == u"line") {
        if (event.isEmpty() || event == u"buffer_line_added")
            onLine(msg);
    } else if (bodyType == u"hotlist") {
        onHotlist(body.toArray());
    } else if (bodyType == u"nick_group" || bodyType == u"nicks") {
        onNicks(bufferNameFromRequest(request), body.toObject());
    } else if (bodyType == u"version") {
        m_version = body.toObject().value(u"weechat_version").toString();
        emit syncedChanged();
    } else if (bodyType == u"completion") {
        m_completer->onServerCompletion(body.toObject());
    }
}

bool Session::handleEvent(const QString &event, const QJsonObject &msg)
{
    const qint64 bufferId = toId(msg.value(u"buffer_id"));
    if (event == u"buffer_closing")
        return true;
    if (event == u"buffer_closed") {
        if (Buffer *b = bufferById(bufferId)) {
            const bool wasCurrent = b == m_current;
            if (wasCurrent)
                openPrevious();
            m_buffers.remove(b);
            m_byId.remove(bufferId);
            m_recent.removeAll(b);
            if (m_current == b) {
                m_current = nullptr;
                emit currentChanged();
            }
            b->deleteLater();
            emit totalsChanged();
        }
        return true;
    }
    if (event == u"buffer_cleared") {
        if (Buffer *b = bufferById(bufferId))
            b->lines()->clear();
        return true;
    }
    if (event == u"buffer_moved" || event == u"buffer_merged" || event == u"buffer_unmerged") {
        m_buffersTimer.start(); // numbers shift for many buffers at once: refetch
        return false;           // and still apply the body to this one
    }
    if (event.startsWith(u"nicklist_")) {
        if (Buffer *b = bufferById(bufferId); b && b->nicksLoaded)
            scheduleNickRefresh(b);
        return true;
    }
    if (event == u"upgrade") {
        emit toast(tr("WeeChat is upgrading…"));
        return true;
    }
    if (event == u"upgrade_ended") {
        onOpened();
        return true;
    }
    if (event == u"quit") {
        emit toast(tr("WeeChat is quitting"));
        return true;
    }
    if (event.startsWith(u"input_"))
        return true;
    return false;
}

void Session::onBuffers(const QJsonArray &arr)
{
    QHash<qint64, Buffer *> previous = m_byId;
    QList<Buffer *> list;
    bool added = false;
    for (const auto &v : arr) {
        const BufferInfo info = parseBuffer(v.toObject());
        Buffer *b = previous.take(info.id);
        if (b) {
            b->update(info);
            // On a resync after reconnecting, catch up on what we missed.
            if (info.hasLines && !info.lines.isEmpty())
                b->lines()->setHistory(info.lines);
        } else {
            b = new Buffer(info, this);
            added = true;
        }
        m_byId.insert(info.id, b);
        list << b;
        collectMentions(b);
    }
    // Buffers that no longer exist.
    for (Buffer *gone : std::as_const(previous)) {
        m_byId.remove(gone->id());
        m_recent.removeAll(gone);
        if (gone == m_current) {
            m_current = nullptr;
            emit currentChanged();
        }
        gone->deleteLater();
    }
    if (added || !previous.isEmpty() || list.size() != m_buffers.buffers().size())
        m_buffers.setBuffers(list);
    else
        m_buffers.resort();

    // Replay lines that arrived before their buffer was known.
    const auto pending = std::exchange(m_pendingLines, {});
    for (auto it = pending.begin(); it != pending.end(); ++it)
        for (const auto &msg : it.value())
            onLine(msg);

    if (!m_synced) {
        m_synced = true;
        emit syncedChanged();
        emit stateChanged();
        // Restore the buffer from last time, or show the first channel.
        if (m_current) {
            requestLines(m_current, std::max(m_current->requestedLines(), kOpenLines));
            if (m_current->kind() == Buffer::Channel)
                requestNicks(m_current);
        } else {
            Buffer *target = bufferByName(m_settings->lastBuffer());
            if (!target)
                for (Buffer *b : m_buffers.buffers())
                    if (b->kind() == Buffer::Channel) { target = b; break; }
            if (!target && !m_buffers.buffers().isEmpty())
                target = m_buffers.buffers().first();
            if (target)
                openBuffer(target);
        }
    }
    if (!m_pendingOpen.isEmpty()) {
        if (Buffer *b = bufferByName(m_pendingOpen)) {
            m_pendingOpen.clear();
            openBuffer(b);
        }
    }
    emit totalsChanged();
}

void Session::onBuffer(const QJsonObject &obj, const QString &event)
{
    const BufferInfo info = parseBuffer(obj);
    if (Buffer *b = bufferById(info.id)) {
        b->update(info);
        if (info.hasLines && !info.lines.isEmpty())
            b->lines()->setHistory(info.lines);
        return;
    }
    if (event == u"buffer_opened" || event.isEmpty()) {
        auto *b = new Buffer(info, this);
        m_byId.insert(info.id, b);
        m_buffers.add(b);
        if (!m_pendingOpen.isEmpty() && b->fullName().compare(m_pendingOpen, Qt::CaseInsensitive) == 0) {
            m_pendingOpen.clear();
            openBuffer(b);
        }
        // Replay lines that raced ahead of this buffer.
        const auto lines = m_pendingLines.take(info.id);
        for (const auto &msg : lines)
            onLine(msg);
        // A query opened by someone messaging us arrives here: let the line handler notify.
        emit totalsChanged();
    }
}

void Session::onLine(const QJsonObject &msg)
{
    const QJsonObject body = msg.value(u"body").toObject();
    qint64 bid = toId(msg.value(u"buffer_id"));
    if (bid == 0)
        bid = toId(body.value(u"buffer_id"));
    Buffer *b = bufferById(bid);
    if (!b) {
        // The line beat the buffer list (a channel just joined): hold it, fetch the buffers.
        if (bid != 0) {
            auto &q = m_pendingLines[bid];
            if (q.size() < 200)
                q << msg;
            m_buffersTimer.start();
        }
        return;
    }
    const Line line = parseLine(body);
    if (takeWhoisLine(b, line))
        return; // shown in the whois window instead
    const auto &entry = b->appendLine(line);
    const bool regular = isRegularMessage(line);
    const bool viewing = b == m_current && m_windowActive;

    if (regular && !entry.self) {
        if (b == m_current && !m_windowActive && m_markerArmed) {
            b->lines()->setUnreadMarker(line.id);
            m_markerArmed = false;
        }
        if (viewing) {
            m_readTimer.start(); // keep WeeChat's own hotlist clear for what we're reading
        } else {
            const bool hl = line.highlight || b->kind() == Buffer::Private;
            b->addUnread(hl, false);
            emit totalsChanged();
        }
        if (line.highlight)
            addMention(b, line.id, entry.nick, entry.plain, line.date, true);
        maybeNotify(b, entry);
    } else if (!regular && !viewing && line.displayed && b != m_current) {
        b->addUnread(false, true);
    }
}

void Session::onLines(const QString &bufferRef, const QJsonArray &arr)
{
    bool ok = false;
    const qint64 id = bufferRef.toLongLong(&ok);
    Buffer *b = ok ? bufferById(id) : bufferByName(bufferRef);
    if (!b)
        return;
    QVector<Line> lines;
    lines.reserve(arr.size());
    for (const auto &v : arr)
        lines << parseLine(v.toObject());
    b->setHistory(lines);
    collectMentions(b);
    if (m_markerOnLoad.contains(b->id())) {
        b->lines()->placeUnreadMarker(m_markerOnLoad.take(b->id()));
        emit b->unreadMarkerPlaced();
    }
}

void Session::onHotlist(const QJsonArray &arr)
{
    QHash<qint64, HotlistEntry> entries;
    for (const auto &v : arr) {
        const auto e = parseHotlist(v.toObject());
        entries.insert(e.bufferId, e);
    }
    for (Buffer *b : m_buffers.buffers()) {
        if (b == m_current && m_windowActive) {
            if (entries.contains(b->id()))
                markReadRemote(b);
            continue;
        }
        const auto e = entries.value(b->id());
        const int hl = e.count[3] + (b->kind() == Buffer::Private ? e.count[2] : 0);
        b->setHotlist(e.count[0], e.count[1] + e.count[2] + e.count[3], hl);
    }
    emit totalsChanged();
}

void Session::onNicks(const QString &bufferRef, const QJsonObject &root)
{
    bool ok = false;
    const qint64 id = bufferRef.toLongLong(&ok);
    Buffer *b = ok ? bufferById(id) : bufferByName(bufferRef);
    if (!b)
        return;
    b->nicksLoaded = true;
    b->nicks()->setNicks(parseNicklist(root));
}

void Session::requestLines(Buffer *b, int count)
{
    b->beginHistoryRequest(count);
    send(linesPath(b->id(), count));
}

void Session::requestNicks(Buffer *b)
{
    send(QStringLiteral("GET /api/buffers/%1/nicks").arg(b->id()));
}

void Session::scheduleNickRefresh(Buffer *b)
{
    m_pendingNicks.insert(b->id());
    if (!m_nickTimer.isActive())
        m_nickTimer.start();
}

void Session::maybeNotify(Buffer *b, const LineModel::Entry &e)
{
    if (!m_settings->notifications())
        return;
    const bool privateMsg = b->kind() == Buffer::Private;
    if (!e.line.highlight && !privateMsg)
        return;
    if (e.line.highlight && !privateMsg && isMuted(e.nick))
        return;
    if (b == m_current && m_windowActive && !m_settings->notifyWhenFocused())
        return;
    QString title = privateMsg ? e.nick : tr("%1 in %2").arg(e.nick, b->displayName());
    QString body = e.kind == EventKind::Action ? QStringLiteral("* %1 %2").arg(e.nick, e.plain) : e.plain;
    if (m_settings->emojiShortcodes())
        body = Formatter::replaceShortcodes(body);
    m_notifier->notify(b->fullName(), title, body);
}

void Session::addMention(Buffer *b, qint64 lineId, const QString &nick, const QString &text, const QDateTime &when, bool live)
{
    const QString key = QStringLiteral("%1:%2").arg(b->id()).arg(lineId);
    if (m_mentionKeys.contains(key))
        return;
    m_mentionKeys.insert(key);
    const QVariantMap entry{
        {QStringLiteral("buffer"), b->fullName()},
        {QStringLiteral("bufferName"), b->displayName()},
        {QStringLiteral("lineId"), lineId},
        {QStringLiteral("nick"), nick},
        {QStringLiteral("text"), m_settings->emojiShortcodes() ? Formatter::replaceShortcodes(text) : text},
        {QStringLiteral("date"), when},
    };
    if (live && !(b == m_current && m_windowActive) && !isMuted(nick))
        ++m_unseenMentions;
    // Newest first; history can arrive after live lines, so insert in order.
    qsizetype i = 0;
    while (i < m_allMentions.size() && m_allMentions.at(i).toMap().value(QStringLiteral("date")).toDateTime() > when)
        ++i;
    m_allMentions.insert(i, entry);
    while (m_allMentions.size() > kMaxMentions)
        m_allMentions.removeLast();
    filterMentions();
}

bool Session::isMuted(const QString &nick) const
{
    for (const auto &re : m_muted)
        if (re.match(nick).hasMatch())
            return true;
    return false;
}

void Session::setMuted(const QString &nick, bool muted)
{
    QStringList words = m_settings->mutedNicks().split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
    if (muted) {
        if (isMuted(nick))
            return;
        words << nick;
    } else {
        // Only an exact entry can be taken off; a wildcard that matches stays.
        words.removeIf([&](const QString &w) { return w.compare(nick, Qt::CaseInsensitive) == 0; });
    }
    m_settings->setMutedNicks(words.join(QLatin1Char(' ')));
}

static QList<QRegularExpression> nickPatterns(const QString &list)
{
    QList<QRegularExpression> out;
    const QStringList words = list.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
    for (const QString &w : words)
        out << QRegularExpression(QRegularExpression::wildcardToRegularExpression(w),
                                  QRegularExpression::CaseInsensitiveOption);
    return out;
}

void Session::updateMuted()
{
    m_muted = nickPatterns(m_settings->mutedNicks());
    filterMentions();
}

void Session::updatePreviewless()
{
    m_previewless = nickPatterns(m_settings->noPreviewNicks());
}

bool Session::isPreviewless(const QString &nick) const
{
    for (const auto &re : m_previewless)
        if (re.match(nick).hasMatch())
            return true;
    return false;
}

void Session::filterMentions()
{
    m_mentions.clear();
    for (auto &v : m_allMentions) {
        QVariantMap m = v.toMap();
        const bool muted = isMuted(m.value(QStringLiteral("nick")).toString());
        if (!m.contains(QStringLiteral("muted")) || m.value(QStringLiteral("muted")).toBool() != muted) {
            m[QStringLiteral("muted")] = muted;
            v = m;
        }
        if (!muted)
            m_mentions << v;
    }
    emit mentionsChanged();
}

void Session::collectMentions(Buffer *b)
{
    for (const auto &e : b->lines()->entries())
        if (e.line.highlight && !e.self && isRegularMessage(e.line))
            addMention(b, e.line.id, e.nick, e.plain, e.line.date);
}

void Session::clearMentions()
{
    m_unseenMentions = 0;
    m_mentions.clear();
    m_allMentions.clear(); // keys stay: cleared mentions shouldn't come back with the next history fetch
    emit mentionsChanged();
}

int Session::totalUnread() const
{
    int n = 0;
    for (Buffer *b : m_buffers.buffers())
        if (b->kind() != Buffer::Core && b->kind() != Buffer::Server)
            n += b->unread();
    return n;
}

int Session::totalHighlights() const
{
    int n = 0;
    for (Buffer *b : m_buffers.buffers())
        n += b->highlights();
    return n;
}

void Session::setWindowActive(bool active)
{
    if (m_windowActive == active)
        return;
    m_windowActive = active;
    emit windowActiveChanged();
    if (active) {
        if (m_current && (m_current->unread() > 0 || m_current->lowActivity() > 0))
            markRead(m_current);
        if (m_current)
            m_notifier->withdraw(m_current->fullName());
        m_markerArmed = false;
    } else if (m_current) {
        // The next message to arrive while away gets the "new messages" divider, unless the
        // existing one is still below everything read so far.
        m_markerArmed = true;
    }
}

Buffer *Session::bufferByName(const QString &fullName) const
{
    if (fullName.isEmpty())
        return nullptr;
    for (Buffer *b : m_buffers.buffers())
        if (b->fullName() == fullName)
            return b;
    for (Buffer *b : m_buffers.buffers())
        if (b->fullName().compare(fullName, Qt::CaseInsensitive) == 0)
            return b;
    return nullptr;
}

void Session::openBuffer(Buffer *b)
{
    if (!b)
        return;
    m_historyPos = -1;
    if (b == m_current) {
        markRead(b);
        return;
    }
    if (m_current)
        m_current->lines()->setUnreadMarker(LineModel::kNoMarker);

    const int unreadAtOpen = b->unread();
    m_current = b;
    m_markerArmed = false;
    m_recent.removeAll(b);
    m_recent.prepend(b);
    m_buffers.setActive(b);
    m_settings->setLastBuffer(b->fullName());

    if (!b->historyFetched || b->requestedLines() < kOpenLines) {
        requestLines(b, kOpenLines);
        m_markerOnLoad.insert(b->id(), unreadAtOpen);
    } else {
        b->lines()->placeUnreadMarker(unreadAtOpen);
    }
    if (b->kind() == Buffer::Channel && (!b->nicksLoaded || b->nicks()->rowCount() == 0))
        requestNicks(b);

    emit currentChanged();
    markRead(b);
}

void Session::openBufferByName(const QString &fullName)
{
    if (Buffer *b = bufferByName(fullName))
        openBuffer(b);
}

void Session::openBufferById(qint64 id)
{
    openBuffer(bufferById(id));
}

void Session::openBufferAt(int n)
{
    if (n >= 1 && n <= m_sidebar.rowCount())
        openBuffer(m_sidebar.bufferAt(n - 1));
}

void Session::openRelative(int delta)
{
    const int count = m_sidebar.rowCount();
    if (count == 0)
        return;
    int row = m_current ? m_sidebar.rowOf(m_current) : -1;
    row = ((row < 0 ? 0 : row + delta) % count + count) % count;
    openBuffer(m_sidebar.bufferAt(row));
}

bool Session::openNextUnread()
{
    Buffer *best = nullptr;
    int bestScore = 0;
    const int count = m_sidebar.rowCount();
    for (int r = 0; r < count; ++r) {
        Buffer *b = m_sidebar.bufferAt(r);
        if (!b || b == m_current)
            continue;
        int score = 0;
        if (b->highlights() > 0) score = 3;
        else if (b->kind() == Buffer::Private && b->unread() > 0) score = 2;
        else if (b->unread() > 0) score = 1;
        if (score > bestScore) {
            best = b;
            bestScore = score;
        }
    }
    if (!best)
        return false;
    openBuffer(best);
    return true;
}

void Session::openPrevious()
{
    for (const auto &p : std::as_const(m_recent))
        if (p && p != m_current) {
            openBuffer(p);
            return;
        }
}

void Session::markRead(Buffer *b)
{
    if (!b)
        return;
    const bool hadUnread = b->unread() > 0 || b->lowActivity() > 0;
    b->clearUnread();
    m_notifier->withdraw(b->fullName());
    if (hadUnread || b == m_current)
        markReadRemote(b);
    emit totalsChanged();
}

void Session::markReadRemote(Buffer *b)
{
    if (!connected())
        return;
    // The relay api has no read endpoint; commands sent through /api/input run in the named
    // buffer's context, which is what makes these "current buffer" commands work.
    for (const auto &cmd : {QStringLiteral("/buffer set hotlist -1"), QStringLiteral("/input set_unread_current_buffer")})
        send(QStringLiteral("POST /api/input"),
             {{QStringLiteral("buffer_name"), b->fullName()}, {QStringLiteral("command"), cmd}});
}

void Session::markAllRead()
{
    for (Buffer *b : m_buffers.buffers())
        if (b->unread() > 0 || b->lowActivity() > 0)
            markRead(b);
}

bool Session::command(const QString &text, Buffer *buffer)
{
    Buffer *b = buffer ? buffer : m_current.data();
    if (!b)
        return false;
    return m_conn.send(request(QStringLiteral("POST /api/input"),
                               {{QStringLiteral("buffer_name"), b->fullName()}, {QStringLiteral("command"), text}}));
}

bool Session::sendInput(const QString &text)
{
    if (!m_current || text.isEmpty())
        return false;
    if (!connected()) {
        emit toast(tr("Not connected — message not sent"));
        return false;
    }
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        if (line.isEmpty())
            continue;
        // "/np": what the player picked in Settings is playing. With no player picked it goes
        // to WeeChat as typed, in case a script there defines /np.
        static const QRegularExpression np(QStringLiteral(R"(^/np(\s.*)?$)"), QRegularExpression::CaseInsensitiveOption);
        if (!m_settings->npPlayer().isEmpty() && np.match(line).hasMatch()) {
            const auto r = m_nowPlaying->share(m_settings->npPlayer(), m_settings->npFormat());
            if (r.text.isEmpty()) {
                emit toast(r.error);
                continue;
            }
            line = r.text;
        }
        // ":tada:" → 🎉 in what we send, unless it's a command other than /me.
        const bool isCommand = line.startsWith(u'/') && !line.startsWith(u"//");
        if (m_settings->emojiShortcodes() && (!isCommand || line.startsWith(u"/me ")) && EmojiStore::instance()) {
            static const QRegularExpression re(QStringLiteral(R"re(:([a-z0-9_+\-]{1,40}):)re"));
            QString out;
            qsizetype last = 0;
            auto it = re.globalMatch(line);
            while (it.hasNext()) {
                const auto m = it.next();
                const QString e = EmojiStore::instance()->lookup(m.captured(1));
                if (e.isEmpty())
                    continue;
                out += QStringView(line).mid(last, m.capturedStart() - last);
                out += e;
                last = m.capturedEnd();
            }
            if (last > 0) {
                out += QStringView(line).mid(last);
                line = out;
            }
        }
        command(line, m_current);
    }
    auto &hist = m_current->inputHistory();
    hist.removeAll(text);
    hist.prepend(text);
    while (hist.size() > kMaxInputHistory)
        hist.removeLast();
    m_historyPos = -1;
    return true;
}

QString Session::historyUp(const QString &currentText)
{
    if (!m_current)
        return currentText;
    const auto &hist = m_current->inputHistory();
    if (m_historyPos + 1 >= hist.size())
        return m_historyPos < 0 ? currentText : hist.at(m_historyPos);
    if (m_historyPos < 0)
        m_historyStash = currentText;
    ++m_historyPos;
    return hist.at(m_historyPos);
}

QString Session::historyDown()
{
    if (!m_current || m_historyPos < 0)
        return QString();
    --m_historyPos;
    if (m_historyPos < 0)
        return m_historyStash;
    return m_current->inputHistory().at(m_historyPos);
}

void Session::loadMoreHistory()
{
    Buffer *b = m_current;
    if (!b || !b->hasMoreHistory() || b->loadingHistory())
        return;
    requestLines(b, b->requestedLines() + kMoreLines);
}

void Session::closeBuffer(Buffer *b)
{
    if (!b || b->kind() == Buffer::Core)
        return;
    command(QStringLiteral("/buffer close"), b);
}

void Session::openQuery(const QString &nick)
{
    if (!m_current || nick.isEmpty())
        return;
    const QString server = m_current->server();
    if (Buffer *existing = bufferByName(QStringLiteral("irc.%1.%2").arg(server, nick))) {
        openBuffer(existing);
        return;
    }
    m_pendingOpen = QStringLiteral("irc.%1.%2").arg(server, nick);
    command(QStringLiteral("/query %1").arg(nick));
}

void Session::joinChannel(const QString &channel)
{
    if (!m_current || channel.isEmpty())
        return;
    const QString server = m_current->server();
    if (Buffer *existing = bufferByName(QStringLiteral("irc.%1.%2").arg(server, channel)); existing && existing->joined()) {
        openBuffer(existing);
        return;
    }
    m_pendingOpen = QStringLiteral("irc.%1.%2").arg(server, channel);
    command(QStringLiteral("/join %1").arg(channel));
}

QVariantList Session::switcherResults(const QString &query, int limit) const
{
    struct Hit { int score; Buffer *b; };
    QVector<Hit> hits;
    const QString q = query.trimmed();
    for (Buffer *b : m_buffers.buffers()) {
        if (b->hidden())
            continue;
        int score;
        if (q.isEmpty()) {
            const int mru = int(m_recent.indexOf(b));
            score = (b->highlights() > 0 ? 3000 : 0) + (b->unread() > 0 ? 1500 : 0)
                    + (mru >= 0 ? 1000 - mru * 10 : 0) - b->number();
        } else {
            score = std::max(fuzzyScore(b->displayName(), q), fuzzyScore(b->fullName(), q) - 100);
            if (score < 0)
                continue;
            if (b->highlights() > 0) score += 40;
            else if (b->unread() > 0) score += 20;
            if (const int mru = int(m_recent.indexOf(b)); mru >= 0) score += std::max(0, 30 - mru * 3);
            if (b == m_current) score -= 50;
        }
        hits << Hit{score, b};
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.score > b.score; });
    QVariantList out;
    for (const auto &h : hits) {
        if (out.size() >= limit)
            break;
        out << QVariant::fromValue(h.b);
    }
    return out;
}

void Session::applySettings()
{
    auto &ctx = FormatContext::current();
    const bool joinPartChanged = ctx.showJoinPart != m_settings->showJoinPart();
    ctx.showJoinPart = m_settings->showJoinPart();
    ctx.colors = m_settings->renderColors();
    ctx.emojiShortcodes = m_settings->emojiShortcodes();
    ctx.use24h = m_settings->use24h();
    ++ctx.generation;
    for (Buffer *b : m_buffers.buffers()) {
        if (joinPartChanged)
            b->lines()->refilter();
        else
            b->lines()->refreshFormatting();
        emit b->changed();
    }
}

void Session::refreshFormatting(bool dark, const QString &accent, const QString &link)
{
    auto &ctx = FormatContext::current();
    if (ctx.dark == dark && ctx.accent == accent && ctx.link == link)
        return;
    ctx.dark = dark;
    ctx.accent = accent;
    ctx.link = link;
    ++ctx.generation;
    for (Buffer *b : m_buffers.buffers()) {
        b->lines()->refreshFormatting();
        b->nicks()->refreshColors();
        emit b->changed();
    }
}

void Session::requestWhois(const QString &nick)
{
    Buffer *b = m_current;
    if (!b || b->server().isEmpty() || nick.trimmed().isEmpty())
        return;
    const QString server = b->server();
    m_whois = {{QStringLiteral("nick"), nick.trimmed()},
               {QStringLiteral("server"), server},
               {QStringLiteral("rows"), QVariantList()},
               {QStringLiteral("loading"), true}};
    emit whoisChanged();
    // Run it in the server buffer, so the reply isn't tied to whatever channel is open.
    Buffer *serverBuffer = bufferByName(QStringLiteral("irc.server.") + server);
    command(QStringLiteral("/whois ") + nick.trimmed(), serverBuffer ? serverBuffer : b);
    if (!m_whoisTimer.isSingleShot()) {
        m_whoisTimer.setSingleShot(true);
        m_whoisTimer.setInterval(15'000);
        connect(&m_whoisTimer, &QTimer::timeout, this, [this] {
            if (!m_whois.value(QStringLiteral("loading")).toBool())
                return;
            m_whois.insert(QStringLiteral("loading"), false);
            if (m_whois.value(QStringLiteral("rows")).toList().isEmpty() && m_whois.value(QStringLiteral("userHost")).toString().isEmpty())
                m_whois.insert(QStringLiteral("error"), tr("The server didn't answer."));
            emit whoisChanged();
        });
    }
    m_whoisTimer.start();
}

bool Session::takeWhoisLine(Buffer *b, const Protocol::Line &line)
{
    if (!m_whois.value(QStringLiteral("loading")).toBool()
        || b->server().compare(m_whois.value(QStringLiteral("server")).toString(), Qt::CaseInsensitive) != 0)
        return false;
    const auto part = Protocol::whoisPart(line.tags, Protocol::stripColors(line.message),
                                          m_whois.value(QStringLiteral("nick")).toString());
    if (!part.matched)
        return false;
    if (part.key == u"user") {
        m_whois.insert(QStringLiteral("userHost"), part.userHost);
        m_whois.insert(QStringLiteral("realName"), part.realName);
    } else if (part.key == u"error") {
        m_whois.insert(QStringLiteral("error"), part.text);
    } else if (!part.end) {
        QVariantList rows = m_whois.value(QStringLiteral("rows")).toList();
        auto add = [&rows](const QString &key, const QString &label, const QString &text) {
            rows << QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("label"), label},
                                {QStringLiteral("text"), text}};
        };
        if (part.key == u"idle") {
            // "00 hours 25 minutes 13 seconds, signon at: Fri, 13 Nov 2026 21:00:00"
            QString idle = part.text.section(QStringLiteral(", signon at:"), 0, 0).trimmed();
            static const QRegularExpression zero(QStringLiteral("^(0+ (days?|hours?),?\\s*)+"));
            idle.remove(zero);
            add(part.key, part.label, idle);
            const QString signon = part.text.section(QStringLiteral("signon at:"), 1).trimmed();
            if (!signon.isEmpty())
                add(QStringLiteral("signon"), tr("Signed on"), signon);
        } else {
            add(part.key, part.label, part.text);
        }
        m_whois.insert(QStringLiteral("rows"), rows);
    }
    if (part.end) {
        m_whois.insert(QStringLiteral("loading"), false);
        m_whoisTimer.stop();
        if (!m_whois.contains(QStringLiteral("error")) && m_whois.value(QStringLiteral("userHost")).toString().isEmpty()
            && m_whois.value(QStringLiteral("rows")).toList().isEmpty())
            m_whois.insert(QStringLiteral("error"), tr("%1 isn't on %2 right now.")
                                                      .arg(m_whois.value(QStringLiteral("nick")).toString(),
                                                           m_whois.value(QStringLiteral("server")).toString()));
    }
    emit whoisChanged();
    return true;
}
