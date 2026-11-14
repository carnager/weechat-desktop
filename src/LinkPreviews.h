#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

/// OpenGraph link previews and image detection, fetched once per URL and cached.
class LinkPreviews : public QObject
{
    Q_OBJECT
public:
    explicit LinkPreviews(QObject *parent = nullptr);
    ~LinkPreviews() override;

    /// {status: "loading"|"ready"|"none", type: "image"|"page", url, title, description, image,
    /// site, iw, ih}. Starts a fetch on first request; `updated(url)` fires when the result is in.
    /// With [fetchPages] false, nothing is fetched: only image URLs (by extension) are recognised.
    Q_INVOKABLE QVariantMap get(const QString &url, bool fetchPages = true);
    /// An image's natural size, remembered so its preview has the right size before it loads
    /// next time (no layout shift).
    Q_INVOKABLE void rememberSize(const QString &url, int width, int height);

    /// Where results persist between runs (empty: memory only). Set before the first get().
    void setCacheFile(const QString &path);
    Q_INVOKABLE static bool isImageUrl(const QString &url);

    /// Exposed for tests.
    static QVariantMap parseHtml(const QByteArray &html, const QString &url);

signals:
    void updated(const QString &url);

private:
    void finish(const QString &url, QVariantMap result);
    void load();
    void save();

    QNetworkAccessManager m_nam;
    QHash<QString, QVariantMap> m_cache;
    int m_inFlight = 0;
    QString m_cacheFile;
    QTimer m_saveTimer;
};
