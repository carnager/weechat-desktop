#pragma once

#include "LineModel.h"
#include "NickModel.h"
#include "Protocol.h"

#include <QObject>

/// One WeeChat buffer: metadata, lines, nicklist, unread state and the user's draft.
class Buffer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 bufferId READ id CONSTANT)
    Q_PROPERTY(QString fullName READ fullName NOTIFY changed)
    Q_PROPERTY(QString name READ displayName NOTIFY changed)
    Q_PROPERTY(QString server READ server NOTIFY changed)
    Q_PROPERTY(QString kind READ kindName NOTIFY changed)
    Q_PROPERTY(int number READ number NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString titleHtml READ titleHtml NOTIFY changed)
    Q_PROPERTY(QString ownNick READ ownNick NOTIFY changed)
    Q_PROPERTY(bool hidden READ hidden NOTIFY changed)
    Q_PROPERTY(bool joined READ joined NOTIFY changed)
    Q_PROPERTY(int unread READ unread NOTIFY unreadChanged)
    Q_PROPERTY(int highlights READ highlights NOTIFY unreadChanged)
    Q_PROPERTY(int lowActivity READ lowActivity NOTIFY unreadChanged)
    Q_PROPERTY(QString preview READ preview NOTIFY previewChanged)
    Q_PROPERTY(LineModel *lines READ lines CONSTANT)
    Q_PROPERTY(NickModel *nicks READ nicks CONSTANT)
    Q_PROPERTY(bool hasMoreHistory READ hasMoreHistory NOTIFY historyChanged)
    Q_PROPERTY(bool loadingHistory READ loadingHistory NOTIFY historyChanged)
    /// The history asked for when the buffer was opened has arrived (the view can show it).
    Q_PROPERTY(bool historyReady READ isHistoryReady NOTIFY historyChanged)
    Q_PROPERTY(QString draft READ draft WRITE setDraft NOTIFY draftChanged)

public:
    enum Kind { Core, Server, Channel, Private, Other };

    explicit Buffer(const Protocol::BufferInfo &info, QObject *parent = nullptr);

    qint64 id() const { return m_info.id; }
    QString fullName() const { return m_info.name; }
    QString displayName() const;
    QString server() const;
    Kind kind() const { return m_kind; }
    QString kindName() const;
    int number() const { return m_info.number; }
    QString title() const;
    QString titleHtml() const;
    QString ownNick() const { return m_info.localVariables.value(QStringLiteral("nick")); }
    bool hidden() const { return m_info.hidden; }
    bool joined() const;
    QString localVar(const QString &name) const { return m_info.localVariables.value(name); }

    void update(const Protocol::BufferInfo &info);

    int unread() const { return m_unread; }
    int highlights() const { return m_highlights; }
    int lowActivity() const { return m_low; }
    void setHotlist(int low, int messages, int highlights);
    void addUnread(bool highlight, bool low);
    void clearUnread();

    QString preview() const { return m_preview; }

    LineModel *lines() { return &m_lines; }
    NickModel *nicks() { return &m_nicks; }

    /// Handle a live line. Returns the stored entry.
    const LineModel::Entry &appendLine(const Protocol::Line &line);
    void setHistory(const QVector<Protocol::Line> &lines);
    /// A request for the last [count] lines is on its way.
    void beginHistoryRequest(int count);

    bool hasMoreHistory() const { return m_hasMore; }
    bool loadingHistory() const { return m_loading; }
    bool isHistoryReady() const { return historyFetched; }
    void setLoadingHistory(bool v);
    int requestedLines() const { return m_requested; }

    QString draft() const { return m_draft; }
    void setDraft(const QString &d);

    QStringList &inputHistory() { return m_inputHistory; }

    bool nicksLoaded = false;
    bool historyFetched = false;

signals:
    void changed();
    void unreadChanged();
    void previewChanged();
    void historyChanged();
    void draftChanged();
    /// The "new messages" marker was placed after this buffer's history arrived.
    void unreadMarkerPlaced();

private:
    void updatePreview(const LineModel::Entry &e);

    Protocol::BufferInfo m_info;
    Kind m_kind = Other;
    LineModel m_lines;
    NickModel m_nicks;
    int m_unread = 0;
    int m_highlights = 0;
    int m_low = 0;
    QString m_preview;
    bool m_hasMore = true;
    bool m_loading = false;
    int m_requested = 0;
    QString m_draft;
    QStringList m_inputHistory;
};
