#include "Completer.h"

#include <QRegularExpression>
#include <QSet>

#include "Buffer.h"
#include "EmojiStore.h"
#include "Formatter.h"
#include "LineModel.h"
#include "Session.h"

const QList<Completer::Command> &Completer::commands()
{
    static const QList<Command> list = {
        {"me", "Send an action"},
        {"msg", "Send a private message"},
        {"query", "Open a private conversation"},
        {"join", "Join a channel"},
        {"part", "Leave a channel"},
        {"cycle", "Leave and rejoin a channel"},
        {"topic", "Show or change the topic"},
        {"nick", "Change your nick"},
        {"whois", "Show information about a nick"},
        {"away", "Set or clear away status"},
        {"invite", "Invite a nick to a channel"},
        {"kick", "Kick a nick from the channel"},
        {"kickban", "Kick and ban a nick"},
        {"ban", "Ban a nick or host"},
        {"unban", "Remove a ban"},
        {"quiet", "Quiet a nick or host"},
        {"op", "Give channel operator status"},
        {"deop", "Remove channel operator status"},
        {"voice", "Give voice"},
        {"devoice", "Remove voice"},
        {"mode", "Change channel or user modes"},
        {"notice", "Send a notice"},
        {"ctcp", "Send a CTCP message"},
        {"names", "List nicks in the channel"},
        {"list", "List channels on the server"},
        {"ignore", "Ignore nicks or hosts"},
        {"server", "Manage servers"},
        {"connect", "Connect to a server"},
        {"disconnect", "Disconnect from a server"},
        {"reconnect", "Reconnect to a server"},
        {"quote", "Send raw data to the server"},
        {"close", "Close the buffer"},
        {"buffer", "Manage buffers"},
        {"clear", "Clear the buffer"},
        {"filter", "Manage message filters"},
        {"set", "Change an option"},
        {"unset", "Reset an option"},
        {"alias", "Manage aliases"},
        {"script", "Manage scripts"},
        {"plugin", "Manage plugins"},
        {"save", "Save configuration"},
        {"upgrade", "Upgrade WeeChat without disconnecting"},
        {"allchan", "Run a command in all channels"},
        {"allserv", "Run a command on all servers"},
        {"wallchops", "Notice the channel operators"},
        {"version", "Show the WeeChat version"},
        {"help", "Show help on commands and options"},
    };
    return list;
}

Completer::Completer(Session *session)
    : QObject(session)
    , m_session(session)
{
}

static bool isWordChar(QChar c)
{
    return !c.isSpace();
}

bool Completer::takesNick(const QString &command)
{
    static const QSet<QString> commands{
        QStringLiteral("query"), QStringLiteral("q"), QStringLiteral("msg"), QStringLiteral("m"),
        QStringLiteral("notice"), QStringLiteral("whois"), QStringLiteral("wii"), QStringLiteral("whowas"),
        QStringLiteral("kick"), QStringLiteral("kickban"), QStringLiteral("ban"), QStringLiteral("unban"),
        QStringLiteral("op"), QStringLiteral("deop"), QStringLiteral("voice"), QStringLiteral("devoice"),
        QStringLiteral("halfop"), QStringLiteral("dehalfop"), QStringLiteral("quiet"), QStringLiteral("unquiet"),
        QStringLiteral("invite"), QStringLiteral("ctcp"), QStringLiteral("ignore"), QStringLiteral("dcc")};
    return commands.contains(command.toLower());
}

QVariantMap Completer::context(const QString &text, int cursor) const
{
    cursor = std::clamp(cursor, 0, int(text.size()));
    int start = cursor;
    while (start > 0 && isWordChar(text.at(start - 1)))
        --start;
    const QString word = text.mid(start, cursor - start);
    // "/query <here>": the first argument of a command that takes a nick.
    static const QRegularExpression commandHead(QStringLiteral("^/(\\w+)\\s+$"));
    const auto head = commandHead.match(text.left(start));
    QString kind;
    if (head.hasMatch() && takesNick(head.captured(1)) && !word.startsWith(u'#')) {
        kind = QStringLiteral("target");
    } else if (word.isEmpty()) {
        kind = QString();
    } else if (start == 0 && word.startsWith(u'/') && !word.startsWith(u"//")) {
        kind = QStringLiteral("command");
    } else if (word.startsWith(u':') && word.size() >= 3 && !word.mid(1).contains(u':')) {
        // ":sm" — but not "http://" or "12:30".
        const QChar c = word.at(1);
        if (c.isLetter() || c == u'+' || c == u'-')
            kind = QStringLiteral("emoji");
    } else if (word.startsWith(u'@') && word.size() >= 2) {
        kind = QStringLiteral("mention");
    } else if (word.startsWith(u'#') && word.size() >= 2) {
        kind = QStringLiteral("channel");
    } else {
        kind = QStringLiteral("nick");
    }
    return {{QStringLiteral("kind"), kind},
            {QStringLiteral("start"), start},
            {QStringLiteral("end"), cursor},
            {QStringLiteral("word"), word}};
}

QStringList Completer::nickCandidates(const QString &prefix) const
{
    Buffer *b = m_session->current();
    if (!b)
        return {};
    QStringList out = b->nicks()->complete(prefix, b->ownNick());
    if (b->kind() == Buffer::Private) {
        const QString other = b->localVar(QStringLiteral("channel"));
        if (!other.isEmpty() && other.startsWith(prefix, Qt::CaseInsensitive) && !out.contains(other))
            out.prepend(other);
    }
    return out;
}

QList<QPair<QString, QString>> Completer::targetCandidates(const QString &prefix) const
{
    QList<QPair<QString, QString>> out;
    Buffer *cur = m_session ? m_session->current() : nullptr;
    if (!cur)
        return out;
    const QString server = cur->server();
    const QString own = cur->ownNick();
    QSet<QString> seen{own.toLower()};
    auto add = [&](const QString &nick, const QString &detail) {
        if (nick.isEmpty() || !nick.startsWith(prefix, Qt::CaseInsensitive) || seen.contains(nick.toLower()))
            return;
        seen.insert(nick.toLower());
        out << qMakePair(nick, detail);
    };
    // Here first (people who spoke recently lead), then queries, then the server's other channels.
    for (const QString &n : nickCandidates(prefix))
        add(n, cur->kind() == Buffer::Channel ? cur->nicks()->prefixOf(n) : QString());
    const auto &all = m_session->buffers()->buffers();
    for (Buffer *b : all)
        if (b->kind() == Buffer::Private && b->server() == server)
            add(b->localVar(QStringLiteral("channel")), tr("query"));
    for (Buffer *b : all)
        if (b != cur && b->kind() == Buffer::Channel && b->server() == server)
            for (const QString &n : b->nicks()->complete(prefix, own))
                add(n, b->displayName());
    return out;
}

QVariantList Completer::suggestions(const QString &kind, const QString &word, int limit) const
{
    QVariantList out;
    const bool dark = FormatContext::current().dark;
    if (kind == u"emoji") {
        if (!EmojiStore::instance())
            return out;
        for (const auto &v : EmojiStore::instance()->search(word.mid(1), limit)) {
            const auto m = v.toMap();
            out << QVariantMap{{QStringLiteral("label"), QStringLiteral(":%1:").arg(m.value(QStringLiteral("shortcode")).toString())},
                               {QStringLiteral("insert"), m.value(QStringLiteral("emoji"))},
                               {QStringLiteral("icon"), m.value(QStringLiteral("emoji"))},
                               {QStringLiteral("detail"), m.value(QStringLiteral("name"))}};
        }
    } else if (kind == u"command") {
        const QString q = word.mid(1).toLower();
        for (const auto &c : commands()) {
            const QString name = QString::fromLatin1(c.name);
            if (!name.startsWith(q))
                continue;
            out << QVariantMap{{QStringLiteral("label"), QStringLiteral("/") + name},
                               {QStringLiteral("insert"), QStringLiteral("/%1 ").arg(name)},
                               {QStringLiteral("detail"), tr(c.help)}};
            if (out.size() >= limit)
                break;
        }
    } else if (kind == u"mention" || kind == u"nick") {
        const QString q = kind == u"mention" ? word.mid(1) : word;
        for (const auto &n : nickCandidates(q)) {
            out << QVariantMap{{QStringLiteral("label"), n},
                               {QStringLiteral("insert"), n},
                               {QStringLiteral("color"), Formatter::nickColor(n, dark)},
                               {QStringLiteral("detail"), m_session->current()->nicks()->prefixOf(n)}};
            if (out.size() >= limit)
                break;
        }
    } else if (kind == u"target") {
        for (const auto &[nick, detail] : targetCandidates(word)) {
            out << QVariantMap{{QStringLiteral("label"), nick},
                               {QStringLiteral("insert"), nick},
                               {QStringLiteral("color"), Formatter::nickColor(nick, dark)},
                               {QStringLiteral("detail"), detail}};
            if (out.size() >= limit)
                break;
        }
    } else if (kind == u"channel") {
        Buffer *cur = m_session->current();
        const QString server = cur ? cur->server() : QString();
        for (Buffer *b : m_session->buffers()->buffers()) {
            if (b->kind() != Buffer::Channel)
                continue;
            const QString name = b->displayName();
            if (!name.startsWith(word, Qt::CaseInsensitive))
                continue;
            QVariantMap m{{QStringLiteral("label"), name},
                          {QStringLiteral("insert"), name},
                          {QStringLiteral("detail"), b->server() == server ? QString() : b->server()}};
            if (b->server() == server)
                out.prepend(m);
            else
                out << m;
        }
        while (out.size() > limit)
            out.removeLast();
    }
    return out;
}

QVariantMap Completer::tab(const QString &text, int cursor, bool backwards)
{
    const bool continuing = !m_tab.candidates.isEmpty() && text == m_tab.produced && cursor == m_tab.producedCursor;
    if (!continuing) {
        const auto ctx = context(text, cursor);
        const QString kind = ctx.value(QStringLiteral("kind")).toString();
        const int start = ctx.value(QStringLiteral("start")).toInt();
        const QString word = ctx.value(QStringLiteral("word")).toString();
        m_tab = {};
        m_tab.before = text.left(start);
        m_tab.after = text.mid(cursor);
        if (kind == u"command") {
            for (const auto &s : suggestions(kind, word, 50))
                m_tab.candidates << s.toMap().value(QStringLiteral("insert")).toString().trimmed();
        } else if (kind == u"channel") {
            for (const auto &s : suggestions(kind, word, 50))
                m_tab.candidates << s.toMap().value(QStringLiteral("insert")).toString();
        } else if (kind == u"emoji") {
            for (const auto &s : suggestions(kind, word, 20))
                m_tab.candidates << s.toMap().value(QStringLiteral("insert")).toString();
        } else if (kind == u"target") {
            for (const auto &c : targetCandidates(word))
                m_tab.candidates << c.first;
        } else if (kind == u"nick" || kind == u"mention") {
            m_tab.candidates = nickCandidates(kind == u"mention" ? word.mid(1) : word);
            m_tab.lineStart = start == 0 && kind == u"nick";
        }
        if (m_tab.candidates.isEmpty())
            return {};
        m_tab.index = backwards ? int(m_tab.candidates.size()) - 1 : 0;
    } else {
        const int n = int(m_tab.candidates.size());
        m_tab.index = ((m_tab.index + (backwards ? -1 : 1)) % n + n) % n;
    }

    QString insert = m_tab.candidates.at(m_tab.index);
    // "nick: " at the start of a line, "nick " elsewhere — but don't double a following space.
    QString suffix = m_tab.lineStart ? QStringLiteral(": ") : QStringLiteral(" ");
    if (m_tab.after.startsWith(u' '))
        suffix = m_tab.lineStart ? QStringLiteral(":") : QString();
    insert += suffix;
    const QString result = m_tab.before + insert + m_tab.after;
    m_tab.produced = result;
    m_tab.producedCursor = int(m_tab.before.size() + insert.size());
    return {{QStringLiteral("text"), result}, {QStringLiteral("cursor"), m_tab.producedCursor}};
}
