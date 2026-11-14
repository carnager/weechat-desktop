#pragma once

#include <QColor>
#include <QString>
#include <QStringList>
#include <functional>

/// Turns WeeChat's ANSI-coloured text into the HTML subset Qt Quick's rich text understands:
/// colours and styles, clickable links and #channels, `:shortcode:` emoji and own-nick mentions.
class Formatter
{
public:
    struct Options {
        bool dark = true;
        bool colors = true;            // render mIRC/WeeChat colours at all
        bool emojiShortcodes = true;   // ":smile:" → 😄
        QString ownNick;               // highlighted where mentioned
        QString accent = QStringLiteral("#8ea2ff");
        QString link = QStringLiteral("#a9b7ff");
    };

    struct Result {
        QString html;
        QStringList urls;
    };

    using EmojiLookup = std::function<QString(const QString &shortcode)>;
    static void setEmojiLookup(EmojiLookup lookup);

    static Result toHtml(const QString &ansi, const Options &opt);
    static QString escape(const QString &text);
    /// ":tada:" → 🎉 for known shortcodes; unknown ones are left alone.
    static QString replaceShortcodes(const QString &text);

    /// A stable, theme-appropriate colour for a nick.
    static QColor nickColor(const QString &nick, bool dark);
    /// Lighten/darken [c] until it's readable on the theme's background.
    static QColor readable(QColor c, bool dark);
    /// xterm-256 palette entry, with the 16 base colours tuned per theme.
    static QColor ansiColor(int index, bool dark);

    /// True if [text] is nothing but 1–[max] emoji (whitespace allowed): shown large.
    static bool isEmojiOnly(const QString &text, int max = 6);
    static QStringList findUrls(const QString &plain);
};
