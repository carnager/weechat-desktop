#include "NickModel.h"

#include "Formatter.h"
#include "LineModel.h"

NickModel::NickModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int NickModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_nicks.size());
}

QHash<int, QByteArray> NickModel::roleNames() const
{
    return {{NameRole, "name"}, {PrefixRole, "prefix"}, {ColorRole, "nickColor"},
            {SectionRole, "section"}, {RankRole, "rank"}};
}

int NickModel::modeRank(const QString &prefix)
{
    if (prefix.isEmpty())
        return 9;
    switch (prefix.at(0).unicode()) {
    case '~': return 0;
    case '&': return 1;
    case '@': return 2;
    case '%': return 3;
    case '+': return 4;
    default: return 9;
    }
}

QVariant NickModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_nicks.size())
        return {};
    const auto &n = m_nicks.at(index.row());
    switch (role) {
    case NameRole: return n.name;
    case PrefixRole: return n.prefix;
    case ColorRole: return Formatter::nickColor(n.name, FormatContext::current().dark);
    case RankRole: return modeRank(n.prefix);
    case SectionRole:
        switch (modeRank(n.prefix)) {
        case 0: case 1: case 2: return tr("Operators");
        case 3: return tr("Half-operators");
        case 4: return tr("Voiced");
        default: return tr("Members");
        }
    }
    return {};
}

void NickModel::setNicks(QVector<Protocol::Nick> nicks)
{
    std::stable_sort(nicks.begin(), nicks.end(), [](const Protocol::Nick &a, const Protocol::Nick &b) {
        const int ra = modeRank(a.prefix), rb = modeRank(b.prefix);
        if (ra != rb)
            return ra < rb;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    beginResetModel();
    m_nicks = std::move(nicks);
    endResetModel();
    emit countChanged();
}

void NickModel::refreshColors()
{
    if (!m_nicks.isEmpty())
        emit dataChanged(index(0), index(int(m_nicks.size()) - 1), {ColorRole});
}

bool NickModel::contains(const QString &nick) const
{
    for (const auto &n : m_nicks)
        if (n.name.compare(nick, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

QString NickModel::prefixOf(const QString &nick) const
{
    for (const auto &n : m_nicks)
        if (n.name == nick)
            return n.prefix;
    return {};
}

QStringList NickModel::names() const
{
    QStringList out;
    for (const auto &n : m_nicks)
        out << n.name;
    return out;
}

void NickModel::noteSpoke(const QString &nick, const QDateTime &when)
{
    if (!nick.isEmpty())
        m_lastSpoke.insert(nick.toLower(), when.toMSecsSinceEpoch());
}

QStringList NickModel::complete(const QString &prefix, const QString &exclude) const
{
    QStringList candidates;
    for (const auto &n : m_nicks)
        if (n.name.startsWith(prefix, Qt::CaseInsensitive) && n.name.compare(exclude, Qt::CaseInsensitive) != 0)
            candidates << n.name;
    std::stable_sort(candidates.begin(), candidates.end(), [this](const QString &a, const QString &b) {
        const qint64 ta = m_lastSpoke.value(a.toLower(), 0), tb = m_lastSpoke.value(b.toLower(), 0);
        if (ta != tb)
            return ta > tb;
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return candidates;
}
