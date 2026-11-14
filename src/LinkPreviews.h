#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantMap>

/// OpenGraph link previews and image detection, fetched once per URL and cached.
class LinkPreviews : public QObject
{
    Q_OBJECT
public:
    explicit LinkPreviews(QObject *parent = nullptr);

    /// {status: "loading"|"ready"|"none", type: "image"|"page", url, title, description, image,
    /// site}. Starts a fetch on first request; `updated(url)` fires when the result is in.
    Q_INVOKABLE QVariantMap get(const QString &url);
    Q_INVOKABLE static bool isImageUrl(const QString &url);

    /// Exposed for tests.
    static QVariantMap parseHtml(const QByteArray &html, const QString &url);

signals:
    void updated(const QString &url);

private:
    void finish(const QString &url, QVariantMap result);

    QNetworkAccessManager m_nam;
    QHash<QString, QVariantMap> m_cache;
    int m_inFlight = 0;
};
