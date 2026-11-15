#include "Buffer.h"
#include "Completer.h"
#include "EmojiStore.h"
#include "Formatter.h"
#include "LineModel.h"
#include "LinkPreviews.h"
#include "NowPlaying.h"
#include "Protocol.h"
#include "RelayConnection.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>

using namespace Protocol;

static Line mkLine(qint64 id, const QString &prefix, const QString &msg, QStringList tags = {QStringLiteral("irc_privmsg")})
{
    Line l;
    l.id = id;
    l.date = QDateTime::currentDateTime().addSecs(id - 1000);
    l.prefix = prefix;
    l.message = msg;
    l.tags = tags;
    return l;
}

class CoreTest : public QObject
{
    Q_OBJECT
private slots:
    void nowPlayingKeys()
    {
        QCOMPARE(NowPlaying::keyOf(QStringLiteral("org.mpris.MediaPlayer2.mpd")), QStringLiteral("mpd"));
        QCOMPARE(NowPlaying::keyOf(QStringLiteral("org.mpris.MediaPlayer2.firefox.instance_1_23")), QStringLiteral("firefox"));
        QCOMPARE(NowPlaying::keyOf(QStringLiteral("org.mpris.MediaPlayer2.vlc.instance4242")), QStringLiteral("vlc"));
        QCOMPARE(NowPlaying::keyOf(QStringLiteral("org.mpris.MediaPlayer2.spotify")), QStringLiteral("spotify"));
    }

    void nowPlayingFormat()
    {
        const QVariantMap full{{QStringLiteral("artist"), QStringLiteral("Boards of Canada")},
                               {QStringLiteral("title"), QStringLiteral("Roygbiv")},
                               {QStringLiteral("album"), QStringLiteral("Music Has the Right to Children")},
                               {QStringLiteral("player"), QStringLiteral("mpd")}};
        QCOMPARE(NowPlaying::expand(QStringLiteral("/me np: {track}"), full), QStringLiteral("/me np: Boards of Canada – Roygbiv"));
        QCOMPARE(NowPlaying::expand(QStringLiteral("{title} [{album}] via {player}"), full),
                 QStringLiteral("Roygbiv [Music Has the Right to Children] via mpd"));
        // No artist (a stream, say): {track} is just the title. Newlines in tags never split the line.
        QCOMPARE(NowPlaying::expand(QStringLiteral("/me np: {track}"), {{QStringLiteral("title"), QStringLiteral("Live\nset")}}),
                 QStringLiteral("/me np: Live set"));
    }

    void completionContextAfterNickCommands()
    {
        Completer c(nullptr);
        auto kind = [&](const QString &text) {
            return c.context(text, int(text.size())).value(QStringLiteral("kind")).toString();
        };
        QCOMPARE(kind(QStringLiteral("/query ")), QStringLiteral("target"));   // before typing anything
        QCOMPARE(kind(QStringLiteral("/msg al")), QStringLiteral("target"));
        QCOMPARE(kind(QStringLiteral("/NOTICE al")), QStringLiteral("target"));
        QCOMPARE(kind(QStringLiteral("/kick   bo")), QStringLiteral("target"));
        QCOMPARE(kind(QStringLiteral("/msg #chan")), QStringLiteral("channel")); // channels stay channels
        QCOMPARE(kind(QStringLiteral("/msg alice hel")), QStringLiteral("nick")); // only the first argument
        QCOMPARE(kind(QStringLiteral("/join ")), QString());                    // not a nick command
        QCOMPARE(kind(QStringLiteral("/que")), QStringLiteral("command"));
        QCOMPARE(c.context(QStringLiteral("/query al"), 9).value(QStringLiteral("start")).toInt(), 7);
    }

    void whoisParsing()
    {
        const QStringList t311{QStringLiteral("irc_311"), QStringLiteral("irc_numeric"), QStringLiteral("nick_mini.irc")};
        auto p = Protocol::whoisPart(t311, QStringLiteral("[alice] (user@bot): alice the bot"), QStringLiteral("Alice"));
        QVERIFY(p.matched);
        QCOMPARE(p.userHost, QStringLiteral("user@bot"));
        QCOMPARE(p.realName, QStringLiteral("alice the bot"));

        p = Protocol::whoisPart({QStringLiteral("irc_319"), QStringLiteral("irc_numeric")},
                                QStringLiteral("[alice] #archlinux #weechat"), QStringLiteral("alice"));
        QCOMPARE(p.key, QStringLiteral("channels"));
        QCOMPARE(p.text, QStringLiteral("#archlinux #weechat"));

        p = Protocol::whoisPart({QStringLiteral("irc_330"), QStringLiteral("irc_numeric")},
                                QStringLiteral("[alice] is logged in as alice_acct"), QStringLiteral("alice"));
        QCOMPARE(p.text, QStringLiteral("alice_acct"));

        p = Protocol::whoisPart({QStringLiteral("irc_317"), QStringLiteral("irc_numeric")},
                                QStringLiteral("[alice] idle: 00 hours 02 minutes 05 seconds, signon at: Sat"), QStringLiteral("alice"));
        QCOMPARE(p.text, QStringLiteral("00 hours 02 minutes 05 seconds, signon at: Sat"));

        p = Protocol::whoisPart({QStringLiteral("irc_318"), QStringLiteral("irc_numeric")},
                                QStringLiteral("[alice] End of /WHOIS list."), QStringLiteral("alice"));
        QVERIFY(p.end);

        p = Protocol::whoisPart({QStringLiteral("irc_401"), QStringLiteral("irc_numeric")},
                                QStringLiteral("ghost: No such nick/channel"), QStringLiteral("ghost"));
        QVERIFY(p.end);
        QCOMPARE(p.key, QStringLiteral("error"));
        QCOMPARE(p.text, QStringLiteral("No such nick/channel"));

        // Someone else's whois, and ordinary lines, don't match.
        QVERIFY(!Protocol::whoisPart(t311, QStringLiteral("[bob] (u@h): bob"), QStringLiteral("alice")).matched);
        QVERIFY(!Protocol::whoisPart({QStringLiteral("irc_privmsg")}, QStringLiteral("[alice] hi"), QStringLiteral("alice")).matched);
    }

    void initTestCase()
    {
        static EmojiStore store;
        QVERIFY(store.loadFromFile(QStringLiteral(":/resources/emoji.json")));
        Formatter::setEmojiLookup([](const QString &c) { return EmojiStore::instance()->lookup(c); });
    }

    void hostNormalization()
    {
        QCOMPARE(RelayConnection::normalizeHost(QStringLiteral("example.org")), QStringLiteral("example.org"));
        QCOMPARE(RelayConnection::normalizeHost(QStringLiteral(" https://example.org/api ")), QStringLiteral("example.org"));
        QCOMPARE(RelayConnection::normalizeHost(QStringLiteral("wss://example.org:9000/api")), QStringLiteral("example.org"));
        QCOMPARE(RelayConnection::normalizeHost(QStringLiteral("[::1]:9000")), QStringLiteral("[::1]"));
        QCOMPARE(RelayConnection::normalizeHost(QString()), QString());
        QCOMPARE(RelayConnection::normalizeHost(QStringLiteral("exa mple")), QString());
    }

    void subprotocolToken()
    {
        const auto p = RelayConnection::subprotocols(QStringLiteral("secret"));
        QCOMPARE(p.first(), QStringLiteral("api.weechat"));
        // base64url("plain:secret") without padding
        QCOMPARE(p.last(), QStringLiteral("base64url.bearer.authorization.weechat.cGxhaW46c2VjcmV0"));
    }

    void parseLineAndIds()
    {
        QJsonObject o{{"id", 1.7e3}, {"date", "2024-05-01T12:34:56.123456Z"},
                      {"prefix", QStringLiteral("\x1b[92m@alice")}, {"message", "hi"},
                      {"tags", QJsonArray{"irc_privmsg", "nick_alice"}}, {"highlight", true}, {"displayed", true}};
        const auto doc = QJsonDocument(o);
        const Line l = parseLine(doc.object());
        QCOMPARE(l.id, 1700);
        QVERIFY(l.date.isValid());
        QCOMPARE(l.date.toUTC().time().msec(), 123);
        QCOMPARE(lineNick(l), QStringLiteral("alice"));
        QVERIFY(l.highlight);
        QCOMPARE(bareNick(l.prefix), QStringLiteral("alice"));
    }

    void regularMessages()
    {
        QVERIFY(isRegularMessage(mkLine(1, "bob", "hello")));
        QVERIFY(!isRegularMessage(mkLine(1, "-->", "bob joined", {"irc_join"})));
        QVERIFY(!isRegularMessage(mkLine(1, "", "===== End of backlog", {"no_highlight", "notify_none", "logger_backlog_end"})));
        QVERIFY(isRegularMessage(mkLine(1, "bob", "restored", {"logger_backlog"})));
        QVERIFY(!isRegularMessage(mkLine(1, "<--", "bob left", {"logger_backlog"})));
        QCOMPARE(classify(mkLine(1, "-->", "x", {"irc_join"})), EventKind::Join);
        QCOMPARE(classify(mkLine(1, " *", "bob waves", {"irc_action", "nick_bob"})), EventKind::Action);
    }

    void mentions()
    {
        QVERIFY(mentionsNick(QStringLiteral("hey rasi, look"), QStringLiteral("rasi")));
        QVERIFY(!mentionsNick(QStringLiteral("rasika is here"), QStringLiteral("rasi")));
        QVERIFY(!mentionsNick(QStringLiteral("rasi|away"), QStringLiteral("rasi")));
    }

    void bufferNameFromRequestPath()
    {
        QCOMPARE(bufferNameFromRequest(QStringLiteral("GET /api/buffers/irc.libera.%23weechat/lines?lines=-10")),
                 QStringLiteral("irc.libera.#weechat"));
        QCOMPARE(bufferNameFromRequest(QStringLiteral("GET /api/buffers/12345/nicks")), QStringLiteral("12345"));
    }

    void ansiToHtml()
    {
        Formatter::Options opt;
        opt.ownNick = QStringLiteral("rasi");
        auto r = Formatter::toHtml(QStringLiteral("\x1b[1mbold\x1b[0m <tag> & rasi: see https://example.org/a_(b)."), opt);
        QVERIFY2(r.html.contains(u"font-weight:600\">bold</span>"), qPrintable(r.html));
        QVERIFY(r.html.contains(u"&lt;tag&gt; &amp;"));
        QVERIFY2(r.html.contains(u"<a href=\"https://example.org/a_(b)\""), qPrintable(r.html));
        QCOMPARE(r.urls, QStringList{QStringLiteral("https://example.org/a_(b)")});
        QVERIFY(r.html.contains(u">rasi</span>"));

        r = Formatter::toHtml(QStringLiteral("\x1b[38;5;196mred\x1b[39m plain"), opt);
        QVERIFY2(r.html.contains(u"color:#"), qPrintable(r.html));

        r = Formatter::toHtml(QStringLiteral("join #weechat now"), opt);
        QVERIFY2(r.html.contains(u"href=\"channel:#weechat\""), qPrintable(r.html));

        r = Formatter::toHtml(QStringLiteral("nice :tada: and :notanemoji: at 12:30:00"), opt);
        QVERIFY2(r.html.contains(QStringLiteral("🎉")), qPrintable(r.html));
        QVERIFY(r.html.contains(u":notanemoji:"));
        QVERIFY(r.html.contains(u"12:30:00"));

        // mIRC codes
        r = Formatter::toHtml(QStringLiteral("\x03" "04red\x03 \x02" "b\x02"), opt);
        QVERIFY2(r.html.contains(u"font-weight:600\">b<"), qPrintable(r.html));
    }

    void emojiOnly()
    {
        QVERIFY(Formatter::isEmojiOnly(QStringLiteral("🎉")));
        QVERIFY(Formatter::isEmojiOnly(QStringLiteral("👍🏽 ❤️")));
        QVERIFY(Formatter::isEmojiOnly(QStringLiteral("👨‍👩‍👧")));
        QVERIFY(Formatter::isEmojiOnly(QStringLiteral("🇩🇪")));
        QVERIFY(!Formatter::isEmojiOnly(QStringLiteral("ok 👍")));
        QVERIFY(!Formatter::isEmojiOnly(QStringLiteral("")));
    }

    void emojiSearch()
    {
        auto *e = EmojiStore::instance();
        QCOMPARE(e->lookup(QStringLiteral("tada")), QStringLiteral("🎉"));
        QCOMPARE(e->lookup(QStringLiteral("+1")), QStringLiteral("👍"));
        const auto hits = e->search(QStringLiteral("thu"), 5);
        QVERIFY(!hits.isEmpty());
        QCOMPARE(hits.first().toMap().value(QStringLiteral("emoji")).toString(), QStringLiteral("👍"));
    }

    void historyMerge()
    {
        LineModel m;
        m.setHistory({mkLine(10, "a", "x"), mkLine(11, "b", "y"), mkLine(12, "a", "z")});
        QCOMPARE(m.rowCount(), 3);
        m.append(mkLine(13, "c", "live"));
        QSignalSpy inserted(&m, &QAbstractItemModel::rowsInserted);
        QSignalSpy reset(&m, &QAbstractItemModel::modelReset);
        // Older history plus one line missed while away.
        m.setHistory({mkLine(8, "a", "o"), mkLine(9, "a", "p"), mkLine(10, "a", "x"), mkLine(11, "b", "y"),
                      mkLine(12, "a", "z"), mkLine(13, "c", "live"), mkLine(14, "d", "new")});
        QCOMPARE(reset.count(), 0);
        QCOMPARE(m.rowCount(), 7);
        QCOMPARE(m.data(m.index(0), LineModel::IdRole).toLongLong(), 8);
        QCOMPARE(m.data(m.index(6), LineModel::IdRole).toLongLong(), 14);
        QVERIFY(inserted.count() >= 2);
    }

    void grouping()
    {
        LineModel m;
        m.setHistory({mkLine(1, "a", "x"), mkLine(2, "a", "y"), mkLine(3, "b", "z")});
        QVERIFY(m.data(m.index(0), LineModel::GroupStartRole).toBool());
        QVERIFY(!m.data(m.index(1), LineModel::GroupStartRole).toBool());
        QVERIFY(m.data(m.index(2), LineModel::GroupStartRole).toBool());
    }

    void linksNewestFirstOnce()
    {
        LineModel m;
        m.setHistory({mkLine(1, "a", "see https://a.example/x and www.b.example", {"irc_privmsg", "nick_a"}),
                      mkLine(2, "b", "again https://a.example/x.", {"irc_privmsg", "nick_b"}),
                      mkLine(3, "c", "no links here", {"irc_privmsg", "nick_c"})});
        const QVariantList links = m.links();
        QCOMPARE(links.size(), 2);
        const QVariantMap first = links.at(0).toMap();
        QCOMPARE(first.value("url").toString(), QStringLiteral("https://a.example/x"));
        QCOMPARE(first.value("nick").toString(), QStringLiteral("b")); // the newest posting
        QCOMPARE(first.value("count").toInt(), 2);
        QCOMPARE(links.at(1).toMap().value("url").toString(), QStringLiteral("https://www.b.example"));
    }

    void actionWithColoredNick()
    {
        // As WeeChat sends a /me: the acting nick is wrapped in colour codes.
        LineModel m;
        m.setHistory({mkLine(1, QStringLiteral("\x1b[0m\x1b[38;5;15m\x1b[49m *"),
                             QStringLiteral("\x1b[0m\x1b[0m\x1b[38;5;15m\x1b[49mrasi\x1b[0m waves"),
                             {"irc_privmsg", "irc_action", "nick_rasi"})});
        QCOMPARE(m.data(m.index(0), LineModel::NickRole).toString(), QStringLiteral("rasi"));
        QCOMPARE(m.data(m.index(0), LineModel::PlainRole).toString(), QStringLiteral("waves"));
        const QString html = m.data(m.index(0), LineModel::HtmlRole).toString();
        QVERIFY2(!html.contains(u"[0m") && !html.contains(u"rasi") && html.contains(u"waves"), qPrintable(html));
    }

    void unreadMarker()
    {
        LineModel m;
        m.setHistory({mkLine(1, "a", "x"), mkLine(2, "-->", "j", {"irc_join"}), mkLine(3, "b", "y"), mkLine(4, "c", "z")});
        m.placeUnreadMarker(2);
        QCOMPARE(m.unreadMarker(), 3);
        QVERIFY(m.data(m.index(m.rowForId(3)), LineModel::UnreadMarkerRole).toBool());
        m.placeUnreadMarker(0);
        QCOMPARE(m.unreadMarker(), LineModel::kNoMarker);
        // WeeChat's first line has id 0: it must be usable as a marker.
        LineModel z;
        z.setHistory({mkLine(0, "a", "first"), mkLine(1, "b", "second")});
        z.placeUnreadMarker(2);
        QCOMPARE(z.unreadMarker(), 0);
        QCOMPARE(z.rowOfUnreadMarker(), 0);
    }

    void joinPartFilter()
    {
        auto &ctx = FormatContext::current();
        LineModel m;
        m.setHistory({mkLine(1, "a", "x"), mkLine(2, "-->", "j", {"irc_join"}), mkLine(3, "b", "y")});
        QCOMPARE(m.rowCount(), 3);
        ctx.showJoinPart = false;
        m.refilter();
        QCOMPARE(m.rowCount(), 2);
        ctx.showJoinPart = true;
        m.refilter();
        QCOMPARE(m.rowCount(), 3);
    }

    void openGraph()
    {
        const QByteArray html = "<html><head><title>Fallback</title>"
            "<meta property=\"og:title\" content=\"Hello &amp; welcome\">"
            "<meta content=\"A description\" name=\"description\">"
            "<meta property=\"og:image\" content=\"/img.png\"></head>";
        const auto r = LinkPreviews::parseHtml(html, QStringLiteral("https://example.org/page"));
        QCOMPARE(r.value("title").toString(), QStringLiteral("Hello & welcome"));
        QCOMPARE(r.value("description").toString(), QStringLiteral("A description"));
        QCOMPARE(r.value("image").toString(), QStringLiteral("https://example.org/img.png"));
        QVERIFY(LinkPreviews::isImageUrl(QStringLiteral("https://x.org/cat.JPG?size=large")));
        QVERIFY(!LinkPreviews::isImageUrl(QStringLiteral("https://x.org/page.html")));
    }

    void youtubeOEmbed()
    {
        const QString watch = QStringLiteral("https://www.youtube.com/oembed?url=https://www.youtube.com/watch?v%3DdQw4w9WgXcQ&format=json");
        for (const char *u : {"https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=42", "https://youtu.be/dQw4w9WgXcQ?si=x",
                              "https://m.youtube.com/shorts/dQw4w9WgXcQ", "https://music.youtube.com/watch?v=dQw4w9WgXcQ",
                              "https://youtube.com/live/dQw4w9WgXcQ"})
            QCOMPARE(LinkPreviews::oembedUrl(QString::fromLatin1(u)).toString(QUrl::FullyEncoded), watch);
        QVERIFY(LinkPreviews::oembedUrl(QStringLiteral("https://www.youtube.com/@channel")).isEmpty());
        QVERIFY(LinkPreviews::oembedUrl(QStringLiteral("https://example.org/watch?v=dQw4w9WgXcQ")).isEmpty());

        const QByteArray json = R"({"title":"Never Gonna Give You Up","author_name":"Rick Astley",)"
                                R"("provider_name":"YouTube","thumbnail_url":"https://i.ytimg.com/vi/x/hqdefault.jpg",)"
                                R"("thumbnail_width":480,"thumbnail_height":360})";
        const auto r = LinkPreviews::parseOEmbed(json, QStringLiteral("https://youtu.be/x"));
        QCOMPARE(r.value("status").toString(), QStringLiteral("ready"));
        QCOMPARE(r.value("title").toString(), QStringLiteral("Never Gonna Give You Up"));
        QCOMPARE(r.value("description").toString(), QStringLiteral("Rick Astley"));
        QCOMPARE(r.value("site").toString(), QStringLiteral("YouTube"));
        QCOMPARE(r.value("image").toString(), QStringLiteral("https://i.ytimg.com/vi/x/hqdefault.jpg"));
        QCOMPARE(r.value("iw").toInt(), 480);
        QCOMPARE(LinkPreviews::parseOEmbed("{}", QStringLiteral("https://youtu.be/x")).value("status").toString(),
                 QStringLiteral("none"));
    }
};

QTEST_MAIN(CoreTest)
#include "tst_core.moc"
