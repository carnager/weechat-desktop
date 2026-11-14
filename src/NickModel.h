#pragma once

#include "Protocol.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>

/// A channel's nicklist, ops first, then voiced, then everyone else, each alphabetical.
class NickModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Role { NameRole = Qt::UserRole + 1, PrefixRole, ColorRole, SectionRole, RankRole };

    explicit NickModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setNicks(QVector<Protocol::Nick> nicks);
    void refreshColors();
    bool contains(const QString &nick) const;
    QString prefixOf(const QString &nick) const;
    QStringList names() const;

    /// Record that [nick] just spoke, for completion ordering.
    void noteSpoke(const QString &nick, const QDateTime &when);
    /// Nicks starting with [prefix], most recent speakers first.
    QStringList complete(const QString &prefix, const QString &exclude = {}) const;

signals:
    void countChanged();

private:
    static int modeRank(const QString &prefix);
    QVector<Protocol::Nick> m_nicks;
    QHash<QString, qint64> m_lastSpoke; // lower-cased nick → msecs since epoch
};
