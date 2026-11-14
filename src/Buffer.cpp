#include "Buffer.h"

#include "Formatter.h"

Buffer::Buffer(const Protocol::BufferInfo &info, QObject *parent)
    : QObject(parent)
{
    update(info);
    m_lines.setOwnNick(ownNick());
    if (info.hasLines) {
        m_requested = int(info.lines.size());
        m_lines.setHistory(info.lines);
        for (int i = int(m_lines.entries().size()) - 1; i >= 0; --i) {
            const auto &e = m_lines.entries().at(i);
            if (Protocol::isRegularMessage(e.line)) {
                updatePreview(e);
                break;
            }
        }
        for (const auto &e : m_lines.entries())
            if (Protocol::isRegularMessage(e.line))
                m_nicks.noteSpoke(e.nick, e.line.date);
    }
}

void Buffer::update(const Protocol::BufferInfo &info)
{
    m_info = info;
    m_info.lines.clear(); // lines live in the model
    switch (Protocol::bufferKind(info)) {
    case Protocol::BufferKind::Core: m_kind = Core; break;
    case Protocol::BufferKind::Server: m_kind = Server; break;
    case Protocol::BufferKind::Channel: m_kind = Channel; break;
    case Protocol::BufferKind::Private: m_kind = Private; break;
    case Protocol::BufferKind::Other: m_kind = Other; break;
    }
    m_lines.setOwnNick(ownNick());
    emit changed();
}

QString Buffer::displayName() const
{
    if (!m_info.shortName.isEmpty())
        return m_info.shortName;
    const QString n = m_info.name;
    if (!n.contains(QLatin1Char('.')))
        return n;
    // "irc.libera.#chan" → "#chan", "irc.server.libera" → "libera", "perl.highmon" → "highmon".
    const QString tail = n.section(QLatin1Char('.'), m_kind == Server ? 1 : 2);
    return tail.isEmpty() ? n.section(QLatin1Char('.'), 1) : tail;
}

QString Buffer::server() const
{
    // Only buffers that belong to a server (IRC, or scripts that set the "server" local variable)
    // get their own sidebar group. Everything else (core, fset, relay, script buffers like
    // highmon) shares the WeeChat group instead of making one near-empty group per plugin.
    // relay_raw sets "server" too, so require a chat buffer type or the irc plugin.
    const auto &lv = m_info.localVariables;
    const QString type = lv.value(QStringLiteral("type"));
    const bool chat = lv.value(QStringLiteral("plugin")) == u"irc" || type == u"server"
        || type == u"channel" || type == u"private";
    return chat ? lv.value(QStringLiteral("server")) : QString();
}

QString Buffer::kindName() const
{
    switch (m_kind) {
    case Core: return QStringLiteral("core");
    case Server: return QStringLiteral("server");
    case Channel: return QStringLiteral("channel");
    case Private: return QStringLiteral("private");
    case Other: return QStringLiteral("other");
    }
    return {};
}

QString Buffer::title() const { return Protocol::stripColors(m_info.title).trimmed(); }

QString Buffer::titleHtml() const
{
    const auto &ctx = FormatContext::current();
    Formatter::Options opt;
    opt.dark = ctx.dark;
    opt.colors = ctx.colors;
    opt.emojiShortcodes = ctx.emojiShortcodes;
    opt.link = ctx.link;
    opt.accent = ctx.accent;
    return Formatter::toHtml(m_info.title.trimmed(), opt).html;
}

bool Buffer::joined() const
{
    // IRC sets "nick" only while we're in the channel; parted channels keep their buffer.
    if (m_kind != Channel)
        return true;
    return !m_info.localVariables.value(QStringLiteral("nick")).isEmpty();
}

void Buffer::setHotlist(int low, int messages, int highlights)
{
    if (m_unread == messages && m_highlights == highlights && m_low == low)
        return;
    m_unread = messages;
    m_highlights = highlights;
    m_low = low;
    emit unreadChanged();
}

void Buffer::addUnread(bool highlight, bool low)
{
    if (low)
        ++m_low;
    else
        ++m_unread;
    if (highlight)
        ++m_highlights;
    emit unreadChanged();
}

void Buffer::clearUnread() { setHotlist(0, 0, 0); }

void Buffer::updatePreview(const LineModel::Entry &e)
{
    QString p = e.kind == Protocol::EventKind::Action ? QStringLiteral("* %1 %2").arg(e.nick, e.plain)
                                                      : QStringLiteral("%1: %2").arg(e.nick, e.plain);
    p = Formatter::replaceShortcodes(p).simplified();
    if (p != m_preview) {
        m_preview = p;
        emit previewChanged();
    }
}

const LineModel::Entry &Buffer::appendLine(const Protocol::Line &line)
{
    const auto &e = m_lines.append(line);
    if (Protocol::isRegularMessage(line)) {
        updatePreview(e);
        m_nicks.noteSpoke(e.nick, line.date);
    }
    return e;
}

void Buffer::beginHistoryRequest(int count)
{
    m_requested = count;
    m_loading = true;
    emit historyChanged();
}

void Buffer::setHistory(const QVector<Protocol::Line> &lines)
{
    m_lines.setHistory(lines);
    // The relay only serves what WeeChat holds in memory; fewer lines than asked means that's all.
    m_hasMore = lines.size() >= m_requested;
    m_loading = false;
    historyFetched = true;
    for (const auto &e : m_lines.entries())
        if (Protocol::isRegularMessage(e.line))
            m_nicks.noteSpoke(e.nick, e.line.date);
    emit historyChanged();
}

void Buffer::setLoadingHistory(bool v)
{
    if (m_loading == v)
        return;
    m_loading = v;
    emit historyChanged();
}

void Buffer::setDraft(const QString &d)
{
    if (m_draft == d)
        return;
    m_draft = d;
    emit draftChanged();
}
