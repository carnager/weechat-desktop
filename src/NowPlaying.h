#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

/// "/np": what the chosen MPRIS player is playing. Only the player picked in Settings is ever
/// asked, so a video in a browser tab can't end up in a channel by accident. Exposed to QML as
/// `NowPlaying`.
class NowPlaying : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    /// The MPRIS players on the session bus right now: [{key, name}], sorted by name.
    Q_INVOKABLE QVariantList players() const;
    /// The display name of the player with this key, or the key itself if it isn't running.
    Q_INVOKABLE QString playerName(const QString &key) const;

    struct Result {
        QString text;  // the line to send, empty on error
        QString error; // why not, for a toast
    };
    /// Formats what the player with `key` is playing with `format`.
    Result share(const QString &key, const QString &format) const;

    /// "org.mpris.MediaPlayer2.firefox.instance_1_23" → "firefox": stable across restarts.
    static QString keyOf(const QString &service);
    /// Fills {artist}, {title}, {album}, {player} and {track} ("Artist – Title", or just the
    /// title when there's no artist) into `format`.
    static QString expand(const QString &format, const QVariantMap &fields);
};
