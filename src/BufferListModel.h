#pragma once

#include <QAbstractListModel>
#include <QPointer>
#include <QSortFilterProxyModel>

class Buffer;

/// All buffers in sidebar order: WeeChat's core buffer first, then each server's buffers
/// together (server buffer, then channels and queries by number).
class BufferListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Role {
        BufferRole = Qt::UserRole + 1,
        IdRole,
        NameRole,
        FullNameRole,
        ServerRole,
        KindRole,
        NumberRole,
        UnreadRole,
        HighlightsRole,
        LowRole,
        ActiveRole,
        HiddenRole,
        JoinedRole,
        PreviewRole,
        SectionRole,
    };

    explicit BufferListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setBuffers(const QList<Buffer *> &buffers);
    void add(Buffer *b);
    void remove(Buffer *b);
    void resort();
    void setActive(Buffer *b);

    const QList<Buffer *> &buffers() const { return m_buffers; }
    Q_INVOKABLE Buffer *at(int row) const { return row >= 0 && row < m_buffers.size() ? m_buffers.at(row) : nullptr; }
    int indexOf(Buffer *b) const { return int(m_buffers.indexOf(b)); }

signals:
    void countChanged();

private:
    void watch(Buffer *b);
    void bufferChanged(Buffer *b, const QList<int> &roles);
    static bool lessThan(const Buffer *a, const Buffer *b, const QHash<QString, int> &serverOrder);

    QList<Buffer *> m_buffers;
    QPointer<Buffer> m_active;
};

/// The sidebar's view of the buffer list: a text filter and the "hide special buffers" option.
class BufferFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(bool hideSpecial READ hideSpecial WRITE setHideSpecial NOTIFY hideSpecialChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    explicit BufferFilterModel(QObject *parent = nullptr);

    QString filterText() const { return m_text; }
    void setFilterText(const QString &t);
    bool hideSpecial() const { return m_hideSpecial; }
    void setHideSpecial(bool v);
    int rowCount(const QModelIndex &parent = {}) const override { return QSortFilterProxyModel::rowCount(parent); }

    Q_INVOKABLE Buffer *bufferAt(int row) const;
    Q_INVOKABLE int rowOf(Buffer *b) const;

signals:
    void filterTextChanged();
    void hideSpecialChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
    QString m_text;
    bool m_hideSpecial = false;
};
