#include "Protocol.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QUrl>

namespace Protocol {

qint64 toId(const QJsonValue &v)
{
    if (v.isDouble())
        return static_cast<qint64>(v.toDouble());
    if (v.isString())
        return v.toString().toLongLong();
    return v.toInteger();
}

static QDateTime parseDate(const QString &s)
{
    if (s.isEmpty())
        return {};
    // WeeChat sends microseconds ("…T12:00:00.123456Z"); Qt only understands milliseconds.
    static const QRegularExpression micro(QStringLiteral("(\\.\\d{3})\\d+"));
    QString t = s;
    t.replace(micro, QStringLiteral("\\1"));
    QDateTime d = QDateTime::fromString(t, Qt::ISODateWithMs);
    if (!d.isValid())
        d = QDateTime::fromString(t, Qt::ISODate);
    return d.toLocalTime();
}

Line parseLine(const QJsonObject &o)
{
    Line l;
    l.id = toId(o.value(u"id"));
    l.date = parseDate(o.value(u"date").toString());
    l.prefix = o.value(u"prefix").toString();
    l.message = o.value(u"message").toString();
    for (const auto &t : o.value(u"tags").toArray())
        l.tags << t.toString();
    l.highlight = o.value(u"highlight").toBool();
    l.displayed = o.value(u"displayed").toBool(true);
    l.notifyLevel = o.value(u"notify_level").toInt();
    return l;
}

BufferInfo parseBuffer(const QJsonObject &o)
{
    BufferInfo b;
    b.id = toId(o.value(u"id"));
    b.name = o.value(u"name").toString();
    b.shortName = o.value(u"short_name").toString();
    b.title = o.value(u"title").toString();
    b.type = o.value(u"type").toString();
    b.number = o.value(u"number").toInt();
    b.hidden = o.value(u"hidden").toBool();
    const auto vars = o.value(u"local_variables").toObject();
    for (auto it = vars.begin(); it != vars.end(); ++it)
        b.localVariables.insert(it.key(), it.value().toString());
    if (o.contains(u"lines")) {
        b.hasLines = true;
        for (const auto &l : o.value(u"lines").toArray())
            b.lines << parseLine(l.toObject());
    }
    return b;
}

HotlistEntry parseHotlist(const QJsonObject &o)
{
    HotlistEntry h;
    h.bufferId = toId(o.value(u"buffer_id"));
    h.priority = o.value(u"priority").toInt();
    const auto c = o.value(u"count").toArray();
    for (int i = 0; i < 4 && i < c.size(); ++i)
        h.count[i] = c.at(i).toInt();
    return h;
}

static void flatten(const QJsonObject &group, QVector<Nick> &out, int &rank)
{
    const int myRank = rank++;
    for (const auto &n : group.value(u"nicks").toArray()) {
        const auto o = n.toObject();
        Nick nick;
        nick.prefix = o.value(u"prefix").toString().trimmed();
        nick.name = o.value(u"name").toString();
        nick.color = o.value(u"color_name").toString();
        nick.visible = o.value(u"visible").toBool(true);
        nick.groupRank = myRank;
        if (nick.visible && !nick.name.isEmpty())
            out << nick;
    }
    for (const auto &g : group.value(u"groups").toArray())
        flatten(g.toObject(), out, rank);
}

QVector<Nick> parseNicklist(const QJsonObject &root)
{
    QVector<Nick> out;
    int rank = 0;
    flatten(root, out, rank);
    return out;
}

QString bufferNameFromRequest(const QString &request)
{
    static const QRegularExpression re(QStringLiteral("/api/buffers/([^/?]+)"));
    const auto m = re.match(request);
    if (!m.hasMatch())
        return {};
    return QUrl::fromPercentEncoding(m.captured(1).toUtf8());
}

QString stripColors(const QString &text)
{
    static const QRegularExpression ansi(QStringLiteral("\x1b\\[[0-9;:]*[A-Za-z]"));
    static const QRegularExpression irc(QStringLiteral("\x03(\\d{1,2}(,\\d{1,2})?)?|[\x02\x0f\x16\x1d\x1f]"));
    QString t = text;
    t.remove(ansi);
    t.remove(irc);
    return t;
}

QString bareNick(const QString &prefix)
{
    QString n = stripColors(prefix).trimmed();
    static const QString modes = QStringLiteral("@+%~&!");
    while (!n.isEmpty() && modes.contains(n.front()))
        n.remove(0, 1);
    return n.trimmed();
}

bool hasTag(const Line &line, const QString &tag) { return line.tags.contains(tag); }

QString lineNick(const Line &line)
{
    for (const auto &t : line.tags)
        if (t.startsWith(u"nick_") && t.size() > 5)
            return t.mid(5);
    if (isAction(line)) {
        const QString text = stripColors(line.message).trimmed();
        return text.section(QLatin1Char(' '), 0, 0);
    }
    return bareNick(line.prefix);
}

bool isAction(const Line &line)
{
    return hasTag(line, QStringLiteral("irc_action")) || stripColors(line.prefix).trimmed() == u"*";
}

bool isSelf(const Line &line) { return hasTag(line, QStringLiteral("self_msg")); }

bool isNotice(const Line &line) { return hasTag(line, QStringLiteral("irc_notice")); }

static bool looksLikeChatPrefix(const QString &prefix)
{
    const QString t = stripColors(prefix).trimmed();
    static const QStringList system = {QStringLiteral("-->"), QStringLiteral("<--"), QStringLiteral("--"),
                                       QStringLiteral("==>"), QStringLiteral("==="), QStringLiteral("=!=")};
    return !t.isEmpty() && !system.contains(t);
}

bool isRegularMessage(const Line &line)
{
    if (!line.displayed || stripColors(line.message).trimmed().isEmpty())
        return false;
    if (hasTag(line, QStringLiteral("logger_backlog_end")))
        return false;
    if (hasTag(line, QStringLiteral("logger_backlog")))
        return looksLikeChatPrefix(line.prefix);
    if (!line.tags.isEmpty()) {
        for (const auto &t : line.tags)
            if (t == u"irc_privmsg" || t == u"irc_notice" || t == u"notify_message"
                || t == u"notify_private" || t == u"notify_highlight")
                return true;
        return false;
    }
    return looksLikeChatPrefix(line.prefix);
}

EventKind classify(const Line &line)
{
    const auto &tags = line.tags;
    if (tags.contains(u"irc_join")) return EventKind::Join;
    if (tags.contains(u"irc_part")) return EventKind::Part;
    if (tags.contains(u"irc_quit")) return EventKind::Quit;
    if (tags.contains(u"irc_nick")) return EventKind::Nick;
    if (tags.contains(u"irc_mode")) return EventKind::Mode;
    if (tags.contains(u"irc_topic") || tags.contains(u"irc_332")) return EventKind::Topic;
    if (isAction(line)) return EventKind::Action;
    if (isNotice(line)) return EventKind::Notice;
    if (isRegularMessage(line)) return EventKind::Message;
    // Tagless lines restored from logs: guess from the prefix WeeChat uses.
    const QString p = stripColors(line.prefix).trimmed();
    if (p == u"-->") return EventKind::Join;
    if (p == u"<--") return EventKind::Part;
    return EventKind::System;
}

bool mentionsNick(const QString &text, const QString &nick)
{
    if (nick.isEmpty())
        return false;
    const QString boundary = QStringLiteral("\\w\\[\\]{}\\\\`|^-");
    const QRegularExpression re(QStringLiteral("(?<![%1])%2(?![%1])")
                                    .arg(boundary, QRegularExpression::escape(nick)),
                                QRegularExpression::CaseInsensitiveOption);
    return re.match(text).hasMatch();
}

BufferKind bufferKind(const BufferInfo &b)
{
    const QString type = b.localVariables.value(QStringLiteral("type"));
    if (b.name == u"core.weechat") return BufferKind::Core;
    if (type == u"server") return BufferKind::Server;
    if (type == u"channel") return BufferKind::Channel;
    if (type == u"private") return BufferKind::Private;
    if (type.isEmpty() && b.name.startsWith(u"irc.")) {
        const QString last = b.name.section(QLatin1Char('.'), -1);
        if (last.startsWith(u'#')) return BufferKind::Channel;
    }
    return BufferKind::Other;
}

QJsonObject request(const QString &path, const QJsonObject &body)
{
    QJsonObject o{{QStringLiteral("request"), path}};
    if (!body.isEmpty())
        o.insert(QStringLiteral("body"), body);
    return o;
}

QString encodeBufferName(const QString &name)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(name));
}

WhoisPart whoisPart(const QStringList &tags, const QString &plainMessage, const QString &nick)
{
    WhoisPart part;
    if (!tags.contains(QStringLiteral("irc_numeric")) || nick.isEmpty())
        return part;
    int numeric = -1;
    for (const QString &t : tags) {
        bool ok = false;
        const int n = t.startsWith(u"irc_") ? t.mid(4).toInt(&ok) : -1;
        if (ok) {
            numeric = n;
            break;
        }
    }
    const QString msg = plainMessage.trimmed();

    // Errors: "nick: No such nick/channel" (WeeChat prints these without brackets).
    if (numeric == 401 || numeric == 402 || numeric == 406) {
        if (msg.startsWith(nick + QLatin1Char(':'), Qt::CaseInsensitive) || msg.contains(nick, Qt::CaseInsensitive)) {
            part.matched = part.end = true;
            part.key = QStringLiteral("error");
            const int colon = msg.indexOf(u':');
            part.text = colon >= 0 ? msg.mid(colon + 1).trimmed() : msg;
        }
        return part;
    }

    const QString head = QLatin1Char('[') + nick + QLatin1Char(']');
    if (!msg.startsWith(head, Qt::CaseInsensitive))
        return part;
    QString text = msg.mid(head.size()).trimmed();
    part.matched = true;

    auto strip = [&](const char *re) {
        text.remove(QRegularExpression(QString::fromLatin1(re), QRegularExpression::CaseInsensitiveOption));
        text = text.trimmed();
    };
    switch (numeric) {
    case 311: { // "(user@host): real name"
        part.key = QStringLiteral("user");
        static const QRegularExpression re(QStringLiteral("^\\(([^)]*)\\):?\\s*(.*)$"));
        const auto m = re.match(text);
        part.userHost = m.hasMatch() ? m.captured(1) : text;
        part.realName = m.hasMatch() ? m.captured(2) : QString();
        part.label = QStringLiteral("User");
        part.text = part.userHost;
        return part;
    }
    case 318:
        part.end = true;
        part.key = QStringLiteral("end");
        return part;
    case 312: part.key = QStringLiteral("server"); part.label = QStringLiteral("Server"); break;
    case 319: part.key = QStringLiteral("channels"); part.label = QStringLiteral("Channels"); break;
    case 317:
        part.key = QStringLiteral("idle");
        part.label = QStringLiteral("Idle");
        strip("^idle:\\s*");
        break;
    case 330: {
        part.key = QStringLiteral("account");
        part.label = QStringLiteral("Account");
        // "is logged in as acct" or "acct is logged in as" / "logged in as".
        static const QRegularExpression after(QStringLiteral("logged in as\\s+(\\S+)"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression before(QStringLiteral("^(\\S+)\\s+(is\\s+)?logged in as$"), QRegularExpression::CaseInsensitiveOption);
        if (const auto m = after.match(text); m.hasMatch())
            text = m.captured(1);
        else if (const auto m2 = before.match(text); m2.hasMatch())
            text = m2.captured(1);
        break;
    }
    case 301:
        part.key = QStringLiteral("away");
        part.label = QStringLiteral("Away");
        strip("^is away:?\\s*");
        break;
    case 671:
    case 276:
        part.key = QStringLiteral("secure");
        part.label = QStringLiteral("Connection");
        break;
    case 313: part.key = QStringLiteral("operator"); part.label = QStringLiteral("Operator"); break;
    case 338:
    case 378: part.key = QStringLiteral("host"); part.label = QStringLiteral("Host"); break;
    default: part.key = QStringLiteral("info"); part.label = QStringLiteral("Info"); break;
    }
    part.text = text;
    return part;
}

} // namespace Protocol
