#include "NowPlaying.h"

#include <QRegularExpression>
#include <algorithm>

#ifdef HAVE_DBUS
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#endif

namespace {
const QString kPrefix = QStringLiteral("org.mpris.MediaPlayer2.");
constexpr int kTimeoutMs = 1000; // a hung player mustn't freeze the input

#ifdef HAVE_DBUS
QVariant mprisProperty(const QString &service, const QString &iface, const QString &name)
{
    auto msg = QDBusMessage::createMethodCall(service, QStringLiteral("/org/mpris/MediaPlayer2"),
                                              QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << iface << name;
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, kTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    return reply.arguments().constFirst().value<QDBusVariant>().variant();
}

QStringList services()
{
    QStringList out;
    if (auto *bus = QDBusConnection::sessionBus().interface())
        for (const QString &s : bus->registeredServiceNames().value())
            // playerctld relays whichever player is active (a browser tab, say): exactly
            // what picking one player is meant to avoid.
            if (s.startsWith(kPrefix) && s != kPrefix + QStringLiteral("playerctld"))
                out << s;
    return out;
}

QString serviceFor(const QString &key)
{
    for (const QString &s : services())
        if (NowPlaying::keyOf(s) == key)
            return s;
    return {};
}

QString identity(const QString &service)
{
    const QString name = mprisProperty(service, QStringLiteral("org.mpris.MediaPlayer2"), QStringLiteral("Identity")).toString();
    return name.isEmpty() ? NowPlaying::keyOf(service) : name;
}
#endif
} // namespace

QString NowPlaying::keyOf(const QString &service)
{
    QString key = service.startsWith(kPrefix) ? service.mid(kPrefix.size()) : service;
    // Players that can run twice add an instance suffix: ".instance1234", ".instance_1_23".
    static const QRegularExpression instance(QStringLiteral(R"(\.instance[\d_]*$)"));
    key.remove(instance);
    return key;
}

QString NowPlaying::expand(const QString &format, const QVariantMap &fields)
{
    const QString artist = fields.value(QStringLiteral("artist")).toString();
    const QString title = fields.value(QStringLiteral("title")).toString();
    QVariantMap all = fields;
    all[QStringLiteral("track")] = artist.isEmpty() ? title : artist + QStringLiteral(" – ") + title;
    QString out = format;
    for (auto it = all.cbegin(); it != all.cend(); ++it)
        out.replace(u'{' + it.key() + u'}', it.value().toString());
    // Tags can hold anything; the result must stay one line.
    out.replace(QRegularExpression(QStringLiteral("[\\r\\n]+")), QStringLiteral(" "));
    return out.trimmed();
}

QVariantList NowPlaying::players() const
{
    QVariantList out;
#ifdef HAVE_DBUS
    QStringList seen;
    for (const QString &s : services()) {
        const QString key = keyOf(s);
        if (seen.contains(key))
            continue;
        seen << key;
        out << QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("name"), identity(s)}};
    }
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().compare(b.toMap().value(QStringLiteral("name")).toString(),
                                                                          Qt::CaseInsensitive) < 0;
    });
#endif
    return out;
}

QString NowPlaying::playerName(const QString &key) const
{
#ifdef HAVE_DBUS
    const QString s = serviceFor(key);
    if (!s.isEmpty())
        return identity(s);
#endif
    return key;
}

NowPlaying::Result NowPlaying::share(const QString &key, const QString &format) const
{
#ifdef HAVE_DBUS
    const QString s = serviceFor(key);
    if (s.isEmpty())
        return {{}, tr("%1 isn't running").arg(key)};
    const QString player = identity(s);
    const QString iface = QStringLiteral("org.mpris.MediaPlayer2.Player");
    if (mprisProperty(s, iface, QStringLiteral("PlaybackStatus")).toString() == u"Stopped")
        return {{}, tr("Nothing is playing in %1").arg(player)};

    QVariantMap meta;
    const QVariant v = mprisProperty(s, iface, QStringLiteral("Metadata"));
    if (v.canConvert<QDBusArgument>())
        v.value<QDBusArgument>() >> meta;
    else
        meta = v.toMap();
    const auto str = [&](const char *k) {
        const QVariant x = meta.value(QString::fromLatin1(k));
        // xesam:artist is a list of strings.
        return x.canConvert<QDBusArgument>() ? qdbus_cast<QStringList>(x).join(QStringLiteral(", "))
             : x.typeId() == QMetaType::QStringList ? x.toStringList().join(QStringLiteral(", "))
                                                     : x.toString();
    };
    const QString title = str("xesam:title");
    if (title.isEmpty())
        return {{}, tr("Nothing is playing in %1").arg(player)};
    return {expand(format, {{QStringLiteral("artist"), str("xesam:artist")},
                            {QStringLiteral("title"), title},
                            {QStringLiteral("album"), str("xesam:album")},
                            {QStringLiteral("player"), player}}),
            {}};
#else
    Q_UNUSED(key)
    Q_UNUSED(format)
    return {{}, tr("Built without D-Bus: /np needs it")};
#endif
}
