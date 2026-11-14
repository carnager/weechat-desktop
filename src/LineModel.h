#pragma once

#include "Protocol.h"

#include <QAbstractListModel>
#include <QColor>

struct FormatContext {
    bool dark = true;
    bool colors = true;
    bool emojiShortcodes = true;
    bool showJoinPart = true;
    bool use24h = true;
    QString accent = QStringLiteral("#8ea2ff");
    QString link = QStringLiteral("#a9b7ff");
    int generation = 0;

    static FormatContext &current();
};

/// The lines of one buffer, as the chat view shows them.
class LineModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        DateRole,
        TimeRole,
        FullTimeRole,
        NickRole,
        NickColorRole,
        PrefixRole,
        HtmlRole,
        PlainRole,
        KindRole,
        HighlightRole,
        SelfRole,
        GroupStartRole,
        DayBreakRole,
        DayLabelRole,
        UrlsRole,
        EmojiOnlyRole,
        UnreadMarkerRole,
    };

    struct Entry {
        Protocol::Line line;
        Protocol::EventKind kind = Protocol::EventKind::System;
        QString nick;
        QString plain;
        bool self = false;
        // Rendering cache, invalidated when FormatContext::generation changes.
        mutable int generation = -1;
        mutable QString html;
        mutable QStringList urls;
    };

    explicit LineModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setOwnNick(const QString &nick) { m_ownNick = nick; }

    /// Replace everything with [lines] (oldest first), keeping lines newer than the last one
    /// fetched — those arrived live while the request was in flight. Rows already shown are kept
    /// in place so the view doesn't jump; older history is inserted above them.
    void setHistory(const QVector<Protocol::Line> &lines);
    /// Append one live line. Returns the entry as stored.
    const Entry &append(const Protocol::Line &line);
    void clear();

    /// Re-render after a theme or setting change.
    void refreshFormatting();
    /// Re-apply the join/part filter.
    void refilter();

    int totalLines() const { return int(m_all.size()); }
    qint64 lastLineId() const { return m_all.isEmpty() ? 0 : m_all.last().line.id; }
    const QVector<Entry> &entries() const { return m_all; }

    /// Put the "new messages" divider above the [unread]-th last regular message (0 removes it).
    /// WeeChat numbers lines from 0, so "no marker" needs a value that can't be a line id.
    static constexpr qint64 kNoMarker = -1;
    void placeUnreadMarker(int unread);
    void setUnreadMarker(qint64 lineId);
    qint64 unreadMarker() const { return m_markerId; }

    Q_INVOKABLE int rowForId(qint64 id) const;
    Q_INVOKABLE int rowOfUnreadMarker() const { return rowForId(m_markerId); }
    /// Search towards older lines from [fromRow] (exclusive); -1 if none.
    Q_INVOKABLE int findPrevious(const QString &query, int fromRow) const;
    Q_INVOKABLE int findNext(const QString &query, int fromRow) const;
    Q_INVOKABLE int countMatches(const QString &query) const;
    /// Plain-text transcript of rows [from, to] for copying.
    Q_INVOKABLE QString textOfRows(int from, int to) const;

signals:
    void countChanged();

private:
    Entry makeEntry(const Protocol::Line &line) const;
    bool isVisible(const Entry &e) const;
    void render(const Entry &e) const;
    void trim();
    const Entry &at(int row) const { return m_all.at(m_visible.at(row)); }

    QVector<Entry> m_all;
    QVector<int> m_visible; // indices into m_all
    QString m_ownNick;
    qint64 m_markerId = kNoMarker;
};
