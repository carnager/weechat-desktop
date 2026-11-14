#pragma once

// Plain data types for WeeChat's `relay api` and the pure helpers that interpret them.
// Nothing in here touches the network or Qt Quick, so it is all unit-testable.

#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Protocol {

struct Line {
    qint64 id = 0;
    QDateTime date;
    QString prefix;      // as sent (may contain ANSI colour codes)
    QString message;     // as sent (may contain ANSI colour codes)
    QStringList tags;
    bool highlight = false;
    bool displayed = true;
    int notifyLevel = 0;
};

struct BufferInfo {
    qint64 id = 0;
    QString name;        // full name, e.g. "irc.libera.#weechat"
    QString shortName;
    QString title;
    QString type;        // "formatted" / "free"
    int number = 0;
    bool hidden = false;
    QMap<QString, QString> localVariables;
    QVector<Line> lines;
    bool hasLines = false; // the JSON carried a "lines" array
};

struct HotlistEntry {
    qint64 bufferId = 0;
    int priority = 0;
    int count[4] = {0, 0, 0, 0}; // low, message, private, highlight
};

struct Nick {
    QString prefix;
    QString name;
    QString color;
    bool visible = true;
    int groupRank = 0; // position of the nick's group; lower means higher modes
};

/// Numbers in the relay api can arrive as large doubles (1.7e+15) — read them losslessly.
qint64 toId(const QJsonValue &v);

Line parseLine(const QJsonObject &o);
BufferInfo parseBuffer(const QJsonObject &o);
HotlistEntry parseHotlist(const QJsonObject &o);
/// Flatten WeeChat's nick group tree into a list, groups in order.
QVector<Nick> parseNicklist(const QJsonObject &root);

/// The buffer name in a request path like "GET /api/buffers/irc.libera.%23x/lines?…".
QString bufferNameFromRequest(const QString &request);

/// Remove ANSI escape sequences (and stray IRC control codes).
QString stripColors(const QString &text);

/// Strip a leading IRC mode prefix ("@", "+", …) from a nick.
QString bareNick(const QString &prefix);

/// The nick that wrote this line: the "nick_xxx" tag if present, else the stripped prefix.
QString lineNick(const Line &line);

bool isAction(const Line &line);
bool isSelf(const Line &line);
bool isNotice(const Line &line);
bool hasTag(const Line &line, const QString &tag);

/// True for lines that are something a human said (privmsg/notice/notify-tagged), as opposed to
/// joins, parts and other system lines. See weelay's MessageFormat for the reasoning behind each
/// rule; the logger backlog cases in particular are subtle.
bool isRegularMessage(const Line &line);

enum class EventKind { Message, Action, Notice, Join, Part, Quit, Nick, Mode, Topic, System };
EventKind classify(const Line &line);

/// True if [text] mentions [nick] as a whole word, with IRC nick characters as word characters.
bool mentionsNick(const QString &text, const QString &nick);

enum class BufferKind { Core, Server, Channel, Private, Other };
BufferKind bufferKind(const BufferInfo &b);

/// One line of a /whois reply, as WeeChat prints it ("[nick] text", tagged irc_numeric + irc_NNN).
struct WhoisPart {
    bool matched = false; // the line belongs to the whois of [nick]
    bool end = false;     // 318 "End of /WHOIS", or an error that ends it
    QString key;          // "user", "server", "channels", "idle", "account", "away", "secure", …, "error"
    QString label;        // human label for the row
    QString text;         // the value
    QString userHost;     // 311 only
    QString realName;     // 311 only
};
WhoisPart whoisPart(const QStringList &tags, const QString &plainMessage, const QString &nick);

QJsonObject request(const QString &path, const QJsonObject &body = {});
QString encodeBufferName(const QString &name);

} // namespace Protocol
