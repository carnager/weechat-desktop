#include "Formatter.h"

#include <QRegularExpression>
#include <QStringBuilder>

namespace {

Formatter::EmojiLookup g_emojiLookup;

struct Style {
    int fg = -1;      // palette index, or -2 for rgb
    QColor fgRgb;
    int bg = -1;
    QColor bgRgb;
    bool bold = false, italic = false, underline = false, reverse = false, dim = false, strike = false;

    bool plain() const
    {
        return fg == -1 && bg == -1 && !bold && !italic && !underline && !reverse && !dim && !strike;
    }
    bool operator==(const Style &o) const
    {
        return fg == o.fg && fgRgb == o.fgRgb && bg == o.bg && bgRgb == o.bgRgb && bold == o.bold
               && italic == o.italic && underline == o.underline && reverse == o.reverse && dim == o.dim
               && strike == o.strike;
    }
};

struct Segment {
    QString text;
    Style style;
};

// mIRC colour numbers → xterm palette indices (what WeeChat itself maps them to).
const int kMircToAnsi[16] = {15, 0, 4, 2, 9, 1, 5, 3, 11, 10, 6, 14, 12, 13, 8, 7};

void applySgr(Style &s, const QList<int> &params)
{
    if (params.isEmpty()) {
        s = Style{};
        return;
    }
    for (int i = 0; i < params.size(); ++i) {
        const int p = params[i];
        if (p == 0) s = Style{};
        else if (p == 1) s.bold = true;
        else if (p == 2) s.dim = true;
        else if (p == 3) s.italic = true;
        else if (p == 4) s.underline = true;
        else if (p == 7) s.reverse = true;
        else if (p == 9) s.strike = true;
        else if (p == 22) { s.bold = false; s.dim = false; }
        else if (p == 23) s.italic = false;
        else if (p == 24) s.underline = false;
        else if (p == 27) s.reverse = false;
        else if (p == 29) s.strike = false;
        else if (p >= 30 && p <= 37) s.fg = p - 30;
        else if (p >= 90 && p <= 97) s.fg = p - 90 + 8;
        else if (p == 39) s.fg = -1;
        else if (p >= 40 && p <= 47) s.bg = p - 40;
        else if (p >= 100 && p <= 107) s.bg = p - 100 + 8;
        else if (p == 49) s.bg = -1;
        else if (p == 38 || p == 48) {
            const bool fg = p == 38;
            if (i + 2 < params.size() && params[i + 1] == 5) {
                (fg ? s.fg : s.bg) = params[i + 2];
                i += 2;
            } else if (i + 4 < params.size() && params[i + 1] == 2) {
                (fg ? s.fg : s.bg) = -2;
                (fg ? s.fgRgb : s.bgRgb) = QColor(params[i + 2], params[i + 3], params[i + 4]);
                i += 4;
            }
        }
    }
}

QList<Segment> parse(const QString &in)
{
    QList<Segment> out;
    Style style;
    QString cur;
    auto flush = [&] {
        if (cur.isEmpty())
            return;
        if (!out.isEmpty() && out.last().style == style)
            out.last().text += cur;
        else
            out.append({cur, style});
        cur.clear();
    };

    const int n = in.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = in.at(i);
        if (c == u'\x1b' && i + 1 < n && in.at(i + 1) == u'[') {
            int j = i + 2;
            while (j < n && !in.at(j).isLetter())
                ++j;
            if (j >= n)
                break;
            if (in.at(j) == u'm') {
                flush();
                QList<int> params;
                const auto parts = QStringView(in).mid(i + 2, j - i - 2).split(QRegularExpression(QStringLiteral("[;:]")));
                for (const auto &p : parts)
                    params << (p.isEmpty() ? 0 : p.toInt());
                applySgr(style, params);
            }
            i = j;
            continue;
        }
        // Raw IRC formatting codes, in case colours weren't converted upstream.
        switch (c.unicode()) {
        case 0x02: flush(); style.bold = !style.bold; continue;
        case 0x1d: flush(); style.italic = !style.italic; continue;
        case 0x1f: flush(); style.underline = !style.underline; continue;
        case 0x16: flush(); style.reverse = !style.reverse; continue;
        case 0x1e: flush(); style.strike = !style.strike; continue;
        case 0x0f: flush(); style = Style{}; continue;
        case 0x03: {
            flush();
            int j = i + 1, fg = -1, bg = -1;
            auto readNum = [&](int &val) {
                int digits = 0, v = 0;
                while (j < n && digits < 2 && in.at(j).isDigit()) { v = v * 10 + in.at(j).digitValue(); ++j; ++digits; }
                if (digits) val = v;
                return digits > 0;
            };
            if (readNum(fg) && j + 1 < n && in.at(j) == u',' && in.at(j + 1).isDigit()) {
                ++j;
                readNum(bg);
            }
            if (fg < 0) { style.fg = -1; style.bg = -1; }
            else {
                style.fg = fg < 16 ? kMircToAnsi[fg] : -1;
                if (bg >= 0) style.bg = bg < 16 ? kMircToAnsi[bg] : -1;
            }
            i = j - 1;
            continue;
        }
        default:
            break;
        }
        if (c.unicode() < 0x20 && c != u'\t')
            continue; // other control characters render as boxes
        cur += c;
    }
    flush();
    return out;
}

const QRegularExpression &urlRe()
{
    static const QRegularExpression re(
        QStringLiteral(R"re((?:(?:https?|ftp)://|www\.)[^\s<>"'`]+)re"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

/// Trim punctuation that's almost always sentence, not URL: "see https://x.org/a)." etc.
QString trimUrl(QString url)
{
    while (!url.isEmpty()) {
        const QChar last = url.back();
        if (QStringLiteral(".,;:!?'\"*").contains(last)) {
            url.chop(1);
        } else if (last == u')' && url.count(u'(') < url.count(u')')) {
            url.chop(1);
        } else if (last == u']' && url.count(u'[') < url.count(u']')) {
            url.chop(1);
        } else if (last == u'>') {
            url.chop(1);
        } else {
            break;
        }
    }
    return url;
}

QString shortcodesToEmoji(const QString &text)
{
    if (!g_emojiLookup || !text.contains(u':'))
        return text;
    static const QRegularExpression re(QStringLiteral(R"re(:([a-z0-9_+\-]{1,40}):)re"));
    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString emoji = g_emojiLookup(m.captured(1));
        if (emoji.isEmpty())
            continue;
        out += QStringView(text).mid(last, m.capturedStart() - last);
        out += emoji;
        last = m.capturedEnd();
    }
    if (last == 0)
        return text;
    out += QStringView(text).mid(last);
    return out;
}

} // namespace

void Formatter::setEmojiLookup(EmojiLookup lookup) { g_emojiLookup = std::move(lookup); }

QString Formatter::replaceShortcodes(const QString &text) { return shortcodesToEmoji(text); }

QString Formatter::escape(const QString &text)
{
    QString out;
    out.reserve(text.size() + 16);
    for (const QChar c : text) {
        switch (c.unicode()) {
        case '&': out += QLatin1String("&amp;"); break;
        case '<': out += QLatin1String("&lt;"); break;
        case '>': out += QLatin1String("&gt;"); break;
        case '"': out += QLatin1String("&quot;"); break;
        default: out += c;
        }
    }
    return out;
}

QColor Formatter::ansiColor(int index, bool dark)
{
    // Base 16 tuned per theme so "black on dark" and "white on light" stay readable.
    static const char *darkBase[16] = {
        "#8b93a1", "#ef6b73", "#7fd18b", "#e5c07b", "#6cb6ff", "#d38aea", "#56c8d8", "#c8ccd4",
        "#a0a8b6", "#ff8a8f", "#9be8a3", "#ffd88a", "#93c9ff", "#e6a6f5", "#7ee0ec", "#ffffff"};
    static const char *lightBase[16] = {
        "#1f2328", "#c62f37", "#1f8a3a", "#9a6700", "#0b62d6", "#8e3fb5", "#0a7f8f", "#57606a",
        "#4b535d", "#d1444c", "#2b9a4a", "#b07a00", "#2f7ae5", "#a052c8", "#1595a6", "#6e7781"};
    if (index < 0)
        return {};
    if (index < 16)
        return QColor(QLatin1String(dark ? darkBase[index] : lightBase[index]));
    if (index < 232) {
        const int i = index - 16;
        static const int steps[6] = {0, 95, 135, 175, 215, 255};
        return QColor(steps[(i / 36) % 6], steps[(i / 6) % 6], steps[i % 6]);
    }
    const int g = 8 + (index - 232) * 10;
    return QColor(g, g, g);
}

QColor Formatter::readable(QColor c, bool dark)
{
    if (!c.isValid())
        return c;
    float h, s, l, a;
    c.getHslF(&h, &s, &l, &a);
    if (dark && l < 0.55f)
        l = 0.55f + l * 0.25f;
    else if (!dark && l > 0.45f)
        l = 0.45f - (1.0f - l) * 0.2f;
    return QColor::fromHslF(h, s, l, a);
}

QColor Formatter::nickColor(const QString &nick, bool dark)
{
    // Hues spread around the wheel, skipping the muddy greens; saturation/lightness per theme.
    static const int hues[] = {0, 18, 32, 45, 95, 140, 165, 185, 200, 215, 235, 255, 275, 295, 315, 335};
    quint32 hash = 5381;
    for (const QChar c : nick.toLower())
        hash = ((hash << 5) + hash) + c.unicode();
    const int hue = hues[hash % (sizeof(hues) / sizeof(hues[0]))];
    return dark ? QColor::fromHsl(hue, 170, 172) : QColor::fromHsl(hue, 180, 92);
}

QStringList Formatter::findUrls(const QString &plain)
{
    QStringList urls;
    auto it = urlRe().globalMatch(plain);
    while (it.hasNext()) {
        QString u = trimUrl(it.next().captured(0));
        if (u.startsWith(u"www.", Qt::CaseInsensitive))
            u.prepend(QStringLiteral("https://"));
        if (!urls.contains(u))
            urls << u;
    }
    return urls;
}

static bool isEmojiCodepoint(char32_t c)
{
    return (c >= 0x1F000 && c <= 0x1FAFF) || (c >= 0x2600 && c <= 0x27BF) || (c >= 0x2B00 && c <= 0x2BFF)
           || (c >= 0x2300 && c <= 0x23FF) || (c >= 0x2190 && c <= 0x21FF) || c == 0x00A9 || c == 0x00AE
           || c == 0x203C || c == 0x2049 || c == 0x2122 || c == 0x2139 || (c >= 0x3030 && c <= 0x303D)
           || c == 0x3297 || c == 0x3299 || (c >= 0xE0020 && c <= 0xE007F);
}

bool Formatter::isEmojiOnly(const QString &text, int max)
{
    int count = 0;
    bool any = false;
    const auto ucs = text.toUcs4();
    for (int i = 0; i < ucs.size(); ++i) {
        const char32_t c = ucs[i];
        if (QChar::isSpace(c))
            continue;
        if (c == 0x200D || c == 0xFE0F || c == 0x20E3 || (c >= 0x1F3FB && c <= 0x1F3FF))
            continue; // joiners, variation selector, keycap, skin tones: part of the previous emoji
        if (isEmojiCodepoint(c)) {
            // A ZWJ sequence counts as one.
            if (!(i > 0 && ucs[i - 1] == 0x200D))
                ++count;
            // Regional indicator pairs (flags) count as one.
            if (c >= 0x1F1E6 && c <= 0x1F1FF && i + 1 < ucs.size() && ucs[i + 1] >= 0x1F1E6 && ucs[i + 1] <= 0x1F1FF)
                ++i;
            any = true;
            continue;
        }
        if ((c >= u'0' && c <= u'9') || c == u'#' || c == u'*') {
            if (i + 1 < ucs.size() && (ucs[i + 1] == 0xFE0F || ucs[i + 1] == 0x20E3))
                continue;
        }
        return false;
    }
    return any && count <= max;
}

Formatter::Result Formatter::toHtml(const QString &ansi, const Options &opt)
{
    Result result;
    const QList<Segment> segments = parse(ansi);

    QString plain;
    for (const auto &s : segments)
        plain += s.text;
    result.urls = findUrls(plain);

    const QString nickEsc = escape(opt.ownNick);
    QRegularExpression mentionRe;
    if (!opt.ownNick.isEmpty()) {
        const QString b = QStringLiteral("\\w\\[\\]{}\\\\`|^-");
        mentionRe = QRegularExpression(QStringLiteral("(?<![%1])(%2)(?![%1])").arg(b, QRegularExpression::escape(nickEsc)),
                                       QRegularExpression::CaseInsensitiveOption);
    }
    static const QRegularExpression channelRe(QStringLiteral(R"re((^|[\s(])(#[^\s,:;()<>"'&]{1,64}[^\s,:;()<>"'&.!?]))re"));

    auto renderText = [&](const QString &text) {
        QString t = opt.emojiShortcodes ? shortcodesToEmoji(text) : text;
        t = escape(t);
        if (mentionRe.isValid() && !opt.ownNick.isEmpty())
            t.replace(mentionRe, QStringLiteral("<span style=\"color:%1;font-weight:600\">\\1</span>").arg(opt.accent));
        t.replace(channelRe, QStringLiteral("\\1<a href=\"channel:\\2\" style=\"color:%1;text-decoration:none\">\\2</a>").arg(opt.link));
        return t;
    };

    QString html;
    for (const auto &seg : segments) {
        QString body;
        qsizetype last = 0;
        auto it = urlRe().globalMatch(seg.text);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString url = trimUrl(m.captured(0));
            body += renderText(seg.text.mid(last, m.capturedStart() - last));
            QString href = url;
            if (href.startsWith(u"www.", Qt::CaseInsensitive))
                href.prepend(QStringLiteral("https://"));
            body += QStringLiteral("<a href=\"%1\" style=\"color:%2\">%3</a>")
                        .arg(escape(href), opt.link, escape(url));
            last = m.capturedStart() + url.size();
        }
        body += renderText(seg.text.mid(last));

        const Style &s = seg.style;
        if (s.plain() || !opt.colors) {
            html += body;
            continue;
        }
        QStringList css;
        QColor fg = s.fg == -2 ? s.fgRgb : ansiColor(s.fg, opt.dark);
        QColor bg = s.bg == -2 ? s.bgRgb : ansiColor(s.bg, opt.dark);
        if (s.reverse) {
            std::swap(fg, bg);
            if (!bg.isValid()) bg = QColor(opt.dark ? QStringLiteral("#c8ccd4") : QStringLiteral("#1f2328"));
            if (!fg.isValid()) fg = QColor(opt.dark ? QStringLiteral("#15171c") : QStringLiteral("#ffffff"));
        }
        if (fg.isValid())
            css << QStringLiteral("color:%1").arg((bg.isValid() ? fg : readable(fg, opt.dark)).name());
        if (bg.isValid())
            css << QStringLiteral("background-color:%1").arg(bg.name());
        if (s.bold) css << QStringLiteral("font-weight:600");
        if (s.italic) css << QStringLiteral("font-style:italic");
        if (s.underline || s.strike)
            css << QStringLiteral("text-decoration:%1").arg(s.strike ? QStringLiteral("line-through") : QStringLiteral("underline"));
        if (s.dim && !fg.isValid()) css << QStringLiteral("color:%1").arg(opt.dark ? QStringLiteral("#9097a3") : QStringLiteral("#6e7781"));
        html += QStringLiteral("<span style=\"%1\">%2</span>").arg(css.join(u';'), body);
    }
    result.html = html;
    return result;
}
