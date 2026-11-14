#include "LineModel.h"

#include "Formatter.h"

#include <QLocale>

namespace {
constexpr int kMaxLines = 5000;
constexpr int kTrimTo = 4000;
constexpr qint64 kGroupGapSecs = 5 * 60;

QString kindName(Protocol::EventKind k)
{
    using K = Protocol::EventKind;
    switch (k) {
    case K::Message: return QStringLiteral("message");
    case K::Action: return QStringLiteral("action");
    case K::Notice: return QStringLiteral("notice");
    case K::Join: return QStringLiteral("join");
    case K::Part: return QStringLiteral("part");
    case K::Quit: return QStringLiteral("quit");
    case K::Nick: return QStringLiteral("nick");
    case K::Mode: return QStringLiteral("mode");
    case K::Topic: return QStringLiteral("topic");
    case K::System: return QStringLiteral("system");
    }
    return {};
}

/// Index in [text] just past its first [count] visible characters, treating ANSI escape
/// sequences as zero-width — so "\x1b[38;5;15mrasi\x1b[0m waves" can lose "rasi " cleanly.
qsizetype skipVisible(const QString &text, qsizetype count)
{
    qsizetype i = 0;
    while (i < text.size()) {
        if (text.at(i) == u'\x1b' && i + 1 < text.size() && text.at(i + 1) == u'[') {
            i += 2;
            while (i < text.size() && !text.at(i).isLetter())
                ++i;
            ++i;
            continue;
        }
        if (count == 0)
            break;
        --count;
        ++i;
    }
    return i;
}

QString dayLabel(const QDate &d)
{
    const QDate today = QDate::currentDate();
    if (d == today)
        return QObject::tr("Today");
    if (d == today.addDays(-1))
        return QObject::tr("Yesterday");
    const QLocale l;
    if (d.year() == today.year())
        return l.toString(d, QStringLiteral("dddd, d MMMM"));
    return l.toString(d, QStringLiteral("dddd, d MMMM yyyy"));
}
} // namespace

FormatContext &FormatContext::current()
{
    static FormatContext ctx;
    return ctx;
}

LineModel::LineModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LineModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_visible.size());
}

QHash<int, QByteArray> LineModel::roleNames() const
{
    return {
        {IdRole, "lineId"},
        {DateRole, "date"},
        {TimeRole, "time"},
        {FullTimeRole, "fullTime"},
        {NickRole, "nick"},
        {NickColorRole, "nickColor"},
        {PrefixRole, "prefix"},
        {HtmlRole, "html"},
        {PlainRole, "plain"},
        {KindRole, "kind"},
        {HighlightRole, "highlight"},
        {SelfRole, "self"},
        {GroupStartRole, "groupStart"},
        {DayBreakRole, "dayBreak"},
        {DayLabelRole, "dayLabel"},
        {UrlsRole, "urls"},
        {EmojiOnlyRole, "emojiOnly"},
        {UnreadMarkerRole, "unreadMarker"},
    };
}

LineModel::Entry LineModel::makeEntry(const Protocol::Line &line) const
{
    Entry e;
    e.line = line;
    e.kind = Protocol::classify(line);
    e.nick = Protocol::lineNick(line);
    e.self = Protocol::isSelf(line);
    QString plain = Protocol::stripColors(line.message);
    if (e.kind == Protocol::EventKind::Action && plain.startsWith(e.nick + QLatin1Char(' ')))
        plain = plain.mid(e.nick.size() + 1);
    e.plain = plain;
    return e;
}

bool LineModel::isVisible(const Entry &e) const
{
    if (!e.line.displayed)
        return false;
    if (e.line.tags.contains(QStringLiteral("logger_backlog_end")))
        return false;
    if (!FormatContext::current().showJoinPart) {
        using K = Protocol::EventKind;
        if (e.kind == K::Join || e.kind == K::Part || e.kind == K::Quit)
            return false;
    }
    return true;
}

void LineModel::render(const Entry &e) const
{
    const auto &ctx = FormatContext::current();
    if (e.generation == ctx.generation)
        return;
    Formatter::Options opt;
    opt.dark = ctx.dark;
    opt.colors = ctx.colors;
    opt.emojiShortcodes = ctx.emojiShortcodes;
    opt.accent = ctx.accent;
    opt.link = ctx.link;
    // Only chat lines get the own-nick emphasis; in join/part lines it would just be noise.
    using K = Protocol::EventKind;
    if (e.kind == K::Message || e.kind == K::Action || e.kind == K::Notice)
        opt.ownNick = e.self ? QString() : m_ownNick;
    QString source = e.line.message;
    if (e.kind == K::Action) {
        // Drop the acting nick from the text: the delegate shows it separately.
        const QString plain = Protocol::stripColors(source);
        if (plain.trimmed().startsWith(e.nick + QLatin1Char(' '))) {
            source = source.mid(skipVisible(source, plain.indexOf(e.nick) + e.nick.size() + 1));
        }
    }
    auto r = Formatter::toHtml(source, opt);
    e.html = r.html;
    e.urls = r.urls;
    e.generation = ctx.generation;
}

QVariant LineModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size())
        return {};
    const int row = index.row();
    const Entry &e = at(row);
    const auto &ctx = FormatContext::current();
    using K = Protocol::EventKind;

    switch (role) {
    case IdRole: return e.line.id;
    case DateRole: return e.line.date;
    case TimeRole:
        return QLocale().toString(e.line.date.time(), ctx.use24h ? QStringLiteral("HH:mm") : QStringLiteral("h:mm AP"));
    case FullTimeRole:
        return QLocale().toString(e.line.date, QLocale::LongFormat);
    case NickRole: return e.nick;
    case NickColorRole: return Formatter::nickColor(e.nick, ctx.dark);
    case PrefixRole: return Protocol::stripColors(e.line.prefix).trimmed();
    case HtmlRole: render(e); return e.html;
    case PlainRole: return e.plain;
    case KindRole: return kindName(e.kind);
    case HighlightRole: return e.line.highlight && !e.self;
    case SelfRole: return e.self;
    case UrlsRole: render(e); return e.urls;
    case EmojiOnlyRole:
        return (e.kind == K::Message) && Formatter::isEmojiOnly(e.plain);
    case UnreadMarkerRole: return m_markerId != kNoMarker && e.line.id == m_markerId;
    case DayBreakRole:
    case DayLabelRole: {
        const bool brk = row == 0 || at(row - 1).line.date.date() != e.line.date.date();
        if (role == DayBreakRole)
            return brk;
        return dayLabel(e.line.date.date());
    }
    case GroupStartRole: {
        if (row == 0 || e.kind != K::Message)
            return true;
        const Entry &p = at(row - 1);
        if (p.kind != K::Message || p.nick != e.nick || p.line.highlight != e.line.highlight)
            return true;
        if (p.line.date.date() != e.line.date.date())
            return true;
        if (m_markerId != kNoMarker && e.line.id == m_markerId)
            return true;
        return p.line.date.secsTo(e.line.date) > kGroupGapSecs;
    }
    }
    return {};
}

void LineModel::setHistory(const QVector<Protocol::Line> &lines)
{
    if (lines.isEmpty())
        return;

    if (!m_all.isEmpty()) {
        const qint64 first = m_all.first().line.id;
        const qint64 last = m_all.last().line.id;
        bool overlaps = false;
        for (const auto &l : lines)
            if (l.id >= first && l.id <= last) { overlaps = true; break; }

        if (overlaps) {
            // Merge: older history goes above what's shown, newer lines (missed while
            // disconnected) below. Rows already shown stay put so the view doesn't jump.
            QVector<Entry> older;
            for (const auto &l : lines)
                if (l.id < first)
                    older << makeEntry(l);
            if (!older.isEmpty()) {
                QVector<int> vis;
                for (int i = 0; i < older.size(); ++i)
                    if (isVisible(older[i]))
                        vis << i;
                const int newVisible = int(vis.size());
                if (newVisible > 0)
                    beginInsertRows({}, 0, newVisible - 1);
                const int shift = int(older.size());
                for (int v : std::as_const(m_visible))
                    vis << v + shift;
                older += m_all;
                m_all = std::move(older);
                m_visible = std::move(vis);
                if (newVisible > 0) {
                    endInsertRows();
                    if (newVisible < m_visible.size()) {
                        const auto idx = index(newVisible);
                        emit dataChanged(idx, idx, {GroupStartRole, DayBreakRole});
                    }
                }
            }
            for (const auto &l : lines)
                if (l.id > m_all.last().line.id)
                    append(l);
            emit countChanged();
            return;
        }
    }

    // Disjoint (first load, or a long gap): start over, keeping lines newer than the fetch.
    const qint64 lastFetched = lines.last().id;
    QVector<Entry> live;
    for (const auto &e : std::as_const(m_all))
        if (e.line.id > lastFetched)
            live << e;
    beginResetModel();
    m_all.clear();
    m_visible.clear();
    for (const auto &l : lines)
        m_all << makeEntry(l);
    m_all += live;
    for (int i = 0; i < m_all.size(); ++i)
        if (isVisible(m_all[i]))
            m_visible << i;
    endResetModel();
    emit countChanged();
}

const LineModel::Entry &LineModel::append(const Protocol::Line &line)
{
    Entry e = makeEntry(line);
    const bool visible = isVisible(e);
    if (visible)
        beginInsertRows({}, int(m_visible.size()), int(m_visible.size()));
    m_all << e;
    if (visible) {
        m_visible << int(m_all.size() - 1);
        endInsertRows();
        emit countChanged();
    }
    trim();
    return m_all.last();
}

void LineModel::clear()
{
    beginResetModel();
    m_all.clear();
    m_visible.clear();
    m_markerId = kNoMarker;
    endResetModel();
    emit countChanged();
}

void LineModel::trim()
{
    if (m_all.size() <= kMaxLines)
        return;
    const int drop = int(m_all.size()) - kTrimTo;
    int dropVisible = 0;
    while (dropVisible < m_visible.size() && m_visible[dropVisible] < drop)
        ++dropVisible;
    if (dropVisible > 0)
        beginRemoveRows({}, 0, dropVisible - 1);
    m_all.remove(0, drop);
    m_visible.remove(0, dropVisible);
    for (int &v : m_visible)
        v -= drop;
    if (dropVisible > 0) {
        endRemoveRows();
        emit countChanged();
    }
}

void LineModel::refreshFormatting()
{
    if (!m_visible.isEmpty())
        emit dataChanged(index(0), index(int(m_visible.size()) - 1));
}

void LineModel::refilter()
{
    beginResetModel();
    m_visible.clear();
    for (int i = 0; i < m_all.size(); ++i)
        if (isVisible(m_all[i]))
            m_visible << i;
    endResetModel();
    emit countChanged();
}

void LineModel::setUnreadMarker(qint64 lineId)
{
    if (m_markerId == lineId)
        return;
    const int oldRow = rowForId(m_markerId);
    m_markerId = lineId;
    const int newRow = rowForId(m_markerId);
    for (int r : {oldRow, newRow})
        if (r >= 0)
            emit dataChanged(index(r), index(r), {UnreadMarkerRole, GroupStartRole});
}

void LineModel::placeUnreadMarker(int unread)
{
    if (unread <= 0) {
        setUnreadMarker(kNoMarker);
        return;
    }
    int seen = 0;
    for (int i = int(m_all.size()) - 1; i >= 0; --i) {
        const Entry &e = m_all[i];
        if (e.self)
            break; // you replied after this; nothing above it is unread
        if (Protocol::isRegularMessage(e.line) && ++seen == unread) {
            setUnreadMarker(e.line.id);
            return;
        }
    }
    // Fewer lines loaded than unread: mark the oldest regular message we have, if any.
    for (int i = 0; i < m_all.size(); ++i)
        if (Protocol::isRegularMessage(m_all[i].line) && seen > 0) {
            setUnreadMarker(m_all[i].line.id);
            return;
        }
}

int LineModel::rowForId(qint64 id) const
{
    if (id == kNoMarker)
        return -1;
    for (int r = int(m_visible.size()) - 1; r >= 0; --r)
        if (at(r).line.id == id)
            return r;
    return -1;
}

int LineModel::findPrevious(const QString &query, int fromRow) const
{
    if (query.isEmpty())
        return -1;
    for (int r = std::min(fromRow, int(m_visible.size())) - 1; r >= 0; --r) {
        const auto &e = at(r);
        if (e.plain.contains(query, Qt::CaseInsensitive) || e.nick.contains(query, Qt::CaseInsensitive))
            return r;
    }
    return -1;
}

int LineModel::findNext(const QString &query, int fromRow) const
{
    if (query.isEmpty())
        return -1;
    for (int r = std::max(fromRow + 1, 0); r < m_visible.size(); ++r) {
        const auto &e = at(r);
        if (e.plain.contains(query, Qt::CaseInsensitive) || e.nick.contains(query, Qt::CaseInsensitive))
            return r;
    }
    return -1;
}

int LineModel::countMatches(const QString &query) const
{
    if (query.isEmpty())
        return 0;
    int n = 0;
    for (int r = 0; r < m_visible.size(); ++r) {
        const auto &e = at(r);
        if (e.plain.contains(query, Qt::CaseInsensitive) || e.nick.contains(query, Qt::CaseInsensitive))
            ++n;
    }
    return n;
}

QVariantList LineModel::links() const
{
    QVariantList out;
    QHash<QString, int> seen; // url -> index in out
    for (qsizetype i = m_visible.size() - 1; i >= 0; --i) {
        const Entry &e = at(int(i));
        render(e);
        for (const QString &text : e.urls) {
            QString url = text;
            if (url.startsWith(u"www.", Qt::CaseInsensitive))
                url.prepend(QStringLiteral("https://"));
            if (auto it = seen.constFind(url); it != seen.cend()) {
                QVariantMap m = out.at(*it).toMap();
                m[QStringLiteral("count")] = m.value(QStringLiteral("count")).toInt() + 1;
                out[*it] = m;
                continue;
            }
            seen.insert(url, int(out.size()));
            out << QVariantMap{
                {QStringLiteral("url"), url},
                {QStringLiteral("text"), text},
                {QStringLiteral("nick"), e.nick},
                {QStringLiteral("date"), e.line.date},
                {QStringLiteral("lineId"), e.line.id},
                {QStringLiteral("count"), 1},
            };
        }
    }
    return out;
}

QString LineModel::textOfRows(int from, int to) const
{
    QStringList out;
    using K = Protocol::EventKind;
    for (int r = std::max(0, from); r <= to && r < m_visible.size(); ++r) {
        const auto &e = at(r);
        const QString time = e.line.date.toString(QStringLiteral("HH:mm"));
        if (e.kind == K::Message || e.kind == K::Notice)
            out << QStringLiteral("[%1] <%2> %3").arg(time, e.nick, e.plain);
        else if (e.kind == K::Action)
            out << QStringLiteral("[%1] * %2 %3").arg(time, e.nick, e.plain);
        else
            out << QStringLiteral("[%1] %2 %3").arg(time, Protocol::stripColors(e.line.prefix).trimmed(), e.plain);
    }
    return out.join(QLatin1Char('\n'));
}
