#include "EmojiStore.h"

#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QRawFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace {
EmojiStore *g_instance = nullptr;
constexpr int kMaxRecent = 32;
}

EmojiStore::EmojiStore(QObject *parent)
    : QObject(parent)
{
    g_instance = this;
    QSettings s;
    m_recent = s.value(QStringLiteral("emoji/recent")).toStringList();
}

EmojiStore *EmojiStore::instance() { return g_instance; }

bool EmojiStore::loadFromFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const auto root = QJsonDocument::fromJson(f.readAll()).object();
    const auto groupNames = root.value(u"groups").toArray();

    // Picker tabs, in emojibase's order, skipping "components" (bare skin tones etc.).
    static const QHash<QString, QString> icons = {
        {QStringLiteral("smileys & emotion"), QStringLiteral("😀")},
        {QStringLiteral("people & body"), QStringLiteral("👋")},
        {QStringLiteral("animals & nature"), QStringLiteral("🐻")},
        {QStringLiteral("food & drink"), QStringLiteral("🍔")},
        {QStringLiteral("travel & places"), QStringLiteral("✈️")},
        {QStringLiteral("activities"), QStringLiteral("⚽")},
        {QStringLiteral("objects"), QStringLiteral("💡")},
        {QStringLiteral("symbols"), QStringLiteral("❤️")},
        {QStringLiteral("flags"), QStringLiteral("🏁")},
    };
    for (int i = 0; i < groupNames.size(); ++i) {
        const QString name = groupNames.at(i).toString();
        if (!icons.contains(name))
            continue;
        m_groupIndex << i;
        QString pretty = name;
        pretty[0] = pretty[0].toUpper();
        m_groupNames << pretty;
        m_groupIcons << icons.value(name);
    }

    // Skip emoji newer than the installed emoji font: they'd show up as boxes in the picker.
    QRawFont emojiFont;
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        QString family;
        for (const QString &f : QFontDatabase::families()) {
            if (!f.contains(u"emoji", Qt::CaseInsensitive))
                continue;
            if (family.isEmpty() || f.contains(u"color", Qt::CaseInsensitive))
                family = f;
        }
        if (!family.isEmpty())
            emojiFont = QRawFont::fromFont(QFont(family));
    }
    auto renderable = [&emojiFont](const QString &s) {
        if (!emojiFont.isValid())
            return true;
        for (const char32_t c : s.toUcs4()) {
            if (c == 0x200D || c == 0xFE0F || c == 0xFE0E || c == 0x20E3 || (c >= 0x1F3FB && c <= 0x1F3FF)
                || (c >= 0xE0020 && c <= 0xE007F) || c < 0x80)
                continue;
            if (!emojiFont.supportsCharacter(c))
                return false;
        }
        return true;
    };

    for (const auto &v : root.value(u"emoji").toArray()) {
        const auto a = v.toArray();
        Emoji e;
        e.emoji = a.at(0).toString();
        if (!renderable(e.emoji))
            continue;
        e.name = a.at(1).toString();
        e.group = a.at(2).toInt();
        for (const auto &s : a.at(3).toArray())
            e.shortcodes << s.toString();
        for (const auto &t : a.at(4).toArray())
            e.tags << t.toString();
        m_byEmoji.insert(e.emoji, m_all.size());
        for (const auto &s : std::as_const(e.shortcodes))
            if (!m_byShortcode.contains(s))
                m_byShortcode.insert(s, e.emoji);
        m_all << e;
    }
    // A few classics people type out of habit.
    for (const auto &[alias, target] : {std::pair{"+1", "thumbsup"}, {"-1", "thumbsdown"}, {"lol", "joy"},
                                        {"heart", "heart"}, {"shrug", "person_shrugging"}, {"tada", "tada"}}) {
        const QString t = m_byShortcode.value(QLatin1String(target));
        if (!t.isEmpty() && !m_byShortcode.contains(QLatin1String(alias)))
            m_byShortcode.insert(QLatin1String(alias), t);
    }
    return !m_all.isEmpty();
}

QVariantMap EmojiStore::toVariant(const Emoji &e) const
{
    return {{QStringLiteral("emoji"), e.emoji},
            {QStringLiteral("name"), e.name},
            {QStringLiteral("shortcode"), e.shortcodes.value(0)}};
}

QVariantList EmojiStore::recent() const
{
    QVariantList out;
    for (const auto &r : m_recent) {
        const int i = m_byEmoji.value(r, -1);
        if (i >= 0)
            out << toVariant(m_all.at(i));
    }
    return out;
}

QVariantList EmojiStore::search(const QString &query, int limit) const
{
    const QString q = query.trimmed().toLower();
    if (q.isEmpty())
        return {};
    // Score: exact shortcode < shortcode prefix < word prefix in name < substring. Ties keep
    // emojibase order (roughly popularity within a group); recently used emoji get a boost.
    struct Hit { int score; int index; QString code; };
    QVector<Hit> hits;
    for (int i = 0; i < m_all.size(); ++i) {
        const auto &e = m_all.at(i);
        int best = 1000;
        QString bestCode = e.shortcodes.value(0);
        for (const auto &s : e.shortcodes) {
            int score = 1000;
            if (s == q) score = 0;
            else if (s.startsWith(q)) score = 10 + int(s.size() - q.size());
            else if (s.contains(QLatin1Char('_') + q)) score = 60;
            else if (q.size() >= 3 && s.contains(q)) score = 120;
            if (score < best) { best = score; bestCode = s; }
        }
        if (best >= 1000) {
            const QString name = e.name.toLower();
            if (name.startsWith(q) || name.contains(QLatin1Char(' ') + q)) best = 80;
            else for (const auto &t : e.tags) if (t.startsWith(q)) { best = 100; break; }
        }
        if (best < 1000) {
            if (m_recent.contains(e.emoji))
                best -= 8;
            hits << Hit{best, i, bestCode};
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.score < b.score; });
    QVariantList out;
    for (const auto &h : hits) {
        if (out.size() >= limit)
            break;
        auto v = toVariant(m_all.at(h.index));
        v[QStringLiteral("shortcode")] = h.code;
        out << v;
    }
    return out;
}

QVariantList EmojiStore::group(int index) const
{
    QVariantList out;
    if (index < 0 || index >= m_groupIndex.size())
        return out;
    const int g = m_groupIndex.at(index);
    for (const auto &e : m_all)
        if (e.group == g)
            out << toVariant(e);
    return out;
}

void EmojiStore::use(const QString &emoji)
{
    m_recent.removeAll(emoji);
    m_recent.prepend(emoji);
    while (m_recent.size() > kMaxRecent)
        m_recent.removeLast();
    QSettings().setValue(QStringLiteral("emoji/recent"), m_recent);
    emit recentChanged();
}
