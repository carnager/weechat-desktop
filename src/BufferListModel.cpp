#include "BufferListModel.h"

#include "Buffer.h"

BufferListModel::BufferListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int BufferListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_buffers.size());
}

QHash<int, QByteArray> BufferListModel::roleNames() const
{
    return {
        {BufferRole, "buffer"}, {IdRole, "bufferId"}, {NameRole, "name"}, {FullNameRole, "fullName"},
        {ServerRole, "server"}, {KindRole, "kind"}, {NumberRole, "number"}, {UnreadRole, "unread"},
        {HighlightsRole, "highlights"}, {LowRole, "low"}, {ActiveRole, "active"}, {HiddenRole, "hidden"},
        {JoinedRole, "joined"}, {PreviewRole, "preview"}, {SectionRole, "section"},
    };
}

QVariant BufferListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_buffers.size())
        return {};
    Buffer *b = m_buffers.at(index.row());
    switch (role) {
    case BufferRole: return QVariant::fromValue(b);
    case IdRole: return b->id();
    case NameRole: return b->displayName();
    case FullNameRole: return b->fullName();
    case ServerRole: return b->server();
    case KindRole: return b->kindName();
    case NumberRole: return b->number();
    case UnreadRole: return b->unread();
    case HighlightsRole: return b->highlights();
    case LowRole: return b->lowActivity();
    case ActiveRole: return b == m_active;
    case HiddenRole: return b->hidden();
    case JoinedRole: return b->joined();
    case PreviewRole: return b->preview();
    case SectionRole: return b->server().isEmpty() ? QStringLiteral("WeeChat") : b->server();
    }
    return {};
}

bool BufferListModel::lessThan(const Buffer *a, const Buffer *b, const QHash<QString, int> &serverOrder)
{
    if (a->kind() == Buffer::Core || b->kind() == Buffer::Core)
        return a->kind() == Buffer::Core && b->kind() != Buffer::Core;
    const int sa = serverOrder.value(a->server(), a->number());
    const int sb = serverOrder.value(b->server(), b->number());
    if (a->server() != b->server())
        return sa != sb ? sa < sb : a->server() < b->server();
    if ((a->kind() == Buffer::Server) != (b->kind() == Buffer::Server))
        return a->kind() == Buffer::Server;
    if (a->number() != b->number())
        return a->number() < b->number();
    return a->displayName().compare(b->displayName(), Qt::CaseInsensitive) < 0;
}

void BufferListModel::resort()
{
    QHash<QString, int> serverOrder;
    for (Buffer *b : std::as_const(m_buffers)) {
        const QString s = b->server();
        const int n = b->number();
        if (!serverOrder.contains(s) || serverOrder.value(s) > n)
            serverOrder.insert(s, n);
    }
    QList<Buffer *> sorted = m_buffers;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&](Buffer *a, Buffer *b) { return lessThan(a, b, serverOrder); });
    if (sorted == m_buffers)
        return;
    emit layoutAboutToBeChanged();
    const auto oldPersistent = persistentIndexList();
    QList<Buffer *> before;
    for (const auto &i : oldPersistent)
        before << m_buffers.value(i.row());
    m_buffers = sorted;
    QModelIndexList after;
    for (Buffer *b : before)
        after << index(int(m_buffers.indexOf(b)));
    changePersistentIndexList(oldPersistent, after);
    emit layoutChanged();
}

void BufferListModel::setBuffers(const QList<Buffer *> &buffers)
{
    beginResetModel();
    for (Buffer *b : std::as_const(m_buffers))
        b->disconnect(this);
    m_buffers = buffers;
    for (Buffer *b : std::as_const(m_buffers))
        watch(b);
    endResetModel();
    resort();
    emit countChanged();
}

void BufferListModel::add(Buffer *b)
{
    beginInsertRows({}, int(m_buffers.size()), int(m_buffers.size()));
    m_buffers << b;
    watch(b);
    endInsertRows();
    resort();
    emit countChanged();
}

void BufferListModel::remove(Buffer *b)
{
    const int row = int(m_buffers.indexOf(b));
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    b->disconnect(this);
    m_buffers.removeAt(row);
    endRemoveRows();
    emit countChanged();
}

void BufferListModel::setActive(Buffer *b)
{
    Buffer *old = m_active;
    m_active = b;
    for (Buffer *x : {old, b}) {
        const int row = int(m_buffers.indexOf(x));
        if (x && row >= 0)
            emit dataChanged(index(row), index(row), {ActiveRole});
    }
}

void BufferListModel::watch(Buffer *b)
{
    connect(b, &Buffer::changed, this, [this, b] {
        bufferChanged(b, {});
        resort();
    });
    connect(b, &Buffer::unreadChanged, this, [this, b] { bufferChanged(b, {UnreadRole, HighlightsRole, LowRole}); });
    connect(b, &Buffer::previewChanged, this, [this, b] { bufferChanged(b, {PreviewRole}); });
}

void BufferListModel::bufferChanged(Buffer *b, const QList<int> &roles)
{
    const int row = int(m_buffers.indexOf(b));
    if (row >= 0)
        emit dataChanged(index(row), index(row), roles);
}

// ---------------------------------------------------------------------------

BufferFilterModel::BufferFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    connect(this, &QAbstractItemModel::rowsInserted, this, &BufferFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &BufferFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &BufferFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &BufferFilterModel::countChanged);
}

void BufferFilterModel::setFilterText(const QString &t)
{
    if (m_text == t)
        return;
    beginFilterChange();
    m_text = t;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit filterTextChanged();
    emit countChanged();
}

void BufferFilterModel::setHideSpecial(bool v)
{
    if (m_hideSpecial == v)
        return;
    beginFilterChange();
    m_hideSpecial = v;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit hideSpecialChanged();
    emit countChanged();
}

bool BufferFilterModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const QModelIndex idx = sourceModel()->index(row, 0, parent);
    auto *b = idx.data(BufferListModel::BufferRole).value<Buffer *>();
    if (!b)
        return false;
    if (b->hidden())
        return false;
    if (m_hideSpecial && (b->kind() == Buffer::Other || b->kind() == Buffer::Core))
        return false;
    if (m_text.isEmpty())
        return true;
    return b->displayName().contains(m_text, Qt::CaseInsensitive)
           || b->fullName().contains(m_text, Qt::CaseInsensitive);
}

Buffer *BufferFilterModel::bufferAt(int row) const
{
    return index(row, 0).data(BufferListModel::BufferRole).value<Buffer *>();
}

int BufferFilterModel::rowOf(Buffer *b) const
{
    for (int r = 0; r < rowCount(); ++r)
        if (bufferAt(r) == b)
            return r;
    return -1;
}
