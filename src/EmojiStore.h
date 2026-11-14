#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVector>

/// Emoji data (from emojibase): shortcode lookup, search for `:` completion and the picker,
/// and a persisted list of recently used emoji.
class EmojiStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList groups READ groups CONSTANT)
    Q_PROPERTY(QStringList groupIcons READ groupIcons CONSTANT)
    Q_PROPERTY(QVariantList recent READ recent NOTIFY recentChanged)
public:
    struct Emoji {
        QString emoji;
        QString name;
        int group = 0;
        QStringList shortcodes;
        QStringList tags;
    };

    explicit EmojiStore(QObject *parent = nullptr);
    static EmojiStore *instance();

    bool loadFromFile(const QString &path);

    QStringList groups() const { return m_groupNames; }
    QStringList groupIcons() const { return m_groupIcons; }
    QVariantList recent() const;

    /// Unicode for a shortcode ("thumbsup" → 👍); empty if unknown.
    QString lookup(const QString &shortcode) const { return m_byShortcode.value(shortcode.toLower()); }

    /// Ranked matches for a partial shortcode or name: prefix matches first, then substring.
    Q_INVOKABLE QVariantList search(const QString &query, int limit = 8) const;
    /// All emoji of a picker group (index into `groups`).
    Q_INVOKABLE QVariantList group(int index) const;
    /// Remember [emoji] as recently used.
    Q_INVOKABLE void use(const QString &emoji);

    const QVector<Emoji> &all() const { return m_all; }

signals:
    void recentChanged();

private:
    QVariantMap toVariant(const Emoji &e) const;

    QVector<Emoji> m_all;
    QHash<QString, QString> m_byShortcode;
    QHash<QString, int> m_byEmoji;
    QStringList m_groupNames;
    QStringList m_groupIcons;
    QVector<int> m_groupIndex; // picker group index → emojibase group number
    QStringList m_recent;
};
