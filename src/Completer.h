#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariantList>

class Session;

/// Input completion: nicks (Tab, or "@" popup), "/commands", "#channels" and ":emoji:".
class Completer : public QObject
{
    Q_OBJECT
public:
    explicit Completer(Session *session);

    /// What's being typed at [cursor]: {kind, start, end, word}. kind is "", "nick", "mention",
    /// "command", "channel", "emoji" or "target" (the nick after /query, /msg, /kick, …, even
    /// before anything is typed). Popups open for everything except plain "nick".
    Q_INVOKABLE QVariantMap context(const QString &text, int cursor) const;
    /// Popup entries: {label, insert, detail, icon, color}.
    Q_INVOKABLE QVariantList suggestions(const QString &kind, const QString &word, int limit = 8) const;

    /// WeeChat-style Tab: complete the word at the cursor, cycling on repeated presses.
    /// Returns {text, cursor} or an empty map when there's nothing to complete.
    Q_INVOKABLE QVariantMap tab(const QString &text, int cursor, bool backwards = false);
    Q_INVOKABLE void resetTab() { m_tab = {}; }

    void onServerCompletion(const QJsonObject &body) { Q_UNUSED(body); }

    struct Command { const char *name; const char *help; };
    static const QList<Command> &commands();

    /// Commands whose first argument is a nick.
    static bool takesNick(const QString &command);

private:
    QStringList nickCandidates(const QString &prefix) const;
    /// For "target": {nick, detail} from this channel, open queries and other channels on the
    /// same server, in that order.
    QList<QPair<QString, QString>> targetCandidates(const QString &prefix) const;

    Session *m_session;
    struct TabState {
        QString before, after;  // text around the word being completed
        QStringList candidates;
        int index = -1;
        bool lineStart = false;
        QString produced;       // text we last produced, to detect a repeated Tab
        int producedCursor = -1;
    } m_tab;
};
