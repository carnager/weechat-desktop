#include "LinkPreviews.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSaveFile>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QUrl>
#include <QUrlQuery>

namespace {
constexpr qint64 kMaxBytes = 768 * 1024;
constexpr int kMaxCache = 500;
constexpr qint64 kMaxAgeSecs = 7 * 24 * 3600; // a week; titles and images rarely change

QString decodeEntities(const QString &s)
{
    return QTextDocumentFragment::fromHtml(s).toPlainText().simplified();
}
} // namespace

LinkPreviews::LinkPreviews(QObject *parent)
    : QObject(parent)
{
    m_nam.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    m_nam.setTransferTimeout(10'000);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(2000);
    connect(&m_saveTimer, &QTimer::timeout, this, &LinkPreviews::save);
}

LinkPreviews::~LinkPreviews()
{
    if (m_saveTimer.isActive()) // changes not written yet
        save();
}

void LinkPreviews::setCacheFile(const QString &path)
{
    m_cacheFile = path;
    load();
}

void LinkPreviews::load()
{
    QFile f(m_cacheFile);
    if (m_cacheFile.isEmpty() || !f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject all = QJsonDocument::fromJson(f.readAll()).object();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (auto it = all.begin(); it != all.end(); ++it) {
        QVariantMap e = it.value().toObject().toVariantMap();
        if (now - e.value(QStringLiteral("t")).toLongLong() > kMaxAgeSecs)
            continue;
        // Scraped before oEmbed was used for this site (usually a failure): fetch again.
        if (e.value(QStringLiteral("src")).toString() != u"oembed" && !oembedUrl(it.key()).isEmpty())
            continue;
        m_cache.insert(it.key(), e);
    }
}

void LinkPreviews::save()
{
    if (m_cacheFile.isEmpty())
        return;
    QJsonObject all;
    for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
        const QString status = it->value(QStringLiteral("status")).toString();
        if (status == u"ready" || status == u"none")
            all.insert(it.key(), QJsonObject::fromVariantMap(*it));
    }
    QDir().mkpath(QFileInfo(m_cacheFile).path());
    QSaveFile f(m_cacheFile);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(all).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

void LinkPreviews::rememberSize(const QString &url, int width, int height)
{
    auto it = m_cache.find(url);
    if (it == m_cache.end() || width <= 0 || height <= 0)
        return;
    if (it->value(QStringLiteral("iw")).toInt() == width && it->value(QStringLiteral("ih")).toInt() == height)
        return;
    it->insert(QStringLiteral("iw"), width);
    it->insert(QStringLiteral("ih"), height);
    m_saveTimer.start();
}

bool LinkPreviews::isImageUrl(const QString &url)
{
    static const QRegularExpression re(QStringLiteral(R"re(\.(png|jpe?g|gif|webp|avif|bmp|svg)(\?[^#]*)?(#.*)?$)re"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re.match(QUrl(url).path() + (QUrl(url).hasQuery() ? QStringLiteral("?") + QUrl(url).query() : QString())).hasMatch()
           || re.match(url).hasMatch();
}

QVariantMap LinkPreviews::parseHtml(const QByteArray &bytes, const QString &url)
{
    const QString html = QString::fromUtf8(bytes);
    QHash<QString, QString> meta;
    static const QRegularExpression tagRe(QStringLiteral(R"re(<meta\s[^>]*>)re"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression attrRe(QStringLiteral(R"re(([a-zA-Z:_-]+)\s*=\s*(?:"([^"]*)"|'([^']*)'))re"));
    auto it = tagRe.globalMatch(html);
    while (it.hasNext()) {
        const QString tag = it.next().captured(0);
        QString key, content;
        auto ait = attrRe.globalMatch(tag);
        while (ait.hasNext()) {
            const auto a = ait.next();
            const QString name = a.captured(1).toLower();
            const QString value = a.captured(2).isNull() ? a.captured(3) : a.captured(2);
            if (name == u"property" || name == u"name")
                key = value.toLower();
            else if (name == u"content")
                content = value;
        }
        if (!key.isEmpty() && !content.isEmpty() && !meta.contains(key))
            meta.insert(key, content);
    }
    auto pick = [&](std::initializer_list<const char *> keys) {
        for (const char *k : keys)
            if (const QString v = meta.value(QLatin1String(k)); !v.isEmpty())
                return decodeEntities(v);
        return QString();
    };
    QString title = pick({"og:title", "twitter:title"});
    if (title.isEmpty()) {
        static const QRegularExpression titleRe(QStringLiteral(R"re(<title[^>]*>([^<]*)</title>)re"),
                                                QRegularExpression::CaseInsensitiveOption);
        title = decodeEntities(titleRe.match(html).captured(1));
    }
    QString description = pick({"og:description", "twitter:description", "description"});
    if (description.size() > 300)
        description = description.left(297) + QStringLiteral("…");
    QString image = pick({"og:image", "og:image:url", "twitter:image", "twitter:image:src"});
    if (!image.isEmpty())
        image = QUrl(url).resolved(QUrl(image)).toString();
    const QString site = pick({"og:site_name"});

    if (title.isEmpty() && description.isEmpty() && image.isEmpty())
        return {{QStringLiteral("status"), QStringLiteral("none")}};
    return {{QStringLiteral("status"), QStringLiteral("ready")},
            {QStringLiteral("type"), QStringLiteral("page")},
            {QStringLiteral("url"), url},
            {QStringLiteral("title"), title},
            {QStringLiteral("description"), description},
            {QStringLiteral("image"), image},
            {QStringLiteral("site"), site.isEmpty() ? QUrl(url).host() : site}};
}

QUrl LinkPreviews::oembedUrl(const QString &url)
{
    const QUrl u(url);
    const QString host = u.host().toLower();
    const bool youtube = host == u"youtu.be" || host == u"youtube.com" || host.endsWith(u".youtube.com");
    if (!youtube)
        return {};
    // Normalise to a watch URL, which oEmbed accepts for videos, shorts and live streams alike.
    QString id;
    static const QRegularExpression idRe(QStringLiteral(R"re(^[A-Za-z0-9_-]{11}$)re"));
    if (host == u"youtu.be") {
        id = u.path().section(u'/', 1, 1);
    } else if (u.path() == u"/watch") {
        id = QUrlQuery(u).queryItemValue(QStringLiteral("v"));
    } else {
        const QString first = u.path().section(u'/', 1, 1);
        if (first == u"shorts" || first == u"live" || first == u"embed" || first == u"v")
            id = u.path().section(u'/', 2, 2);
    }
    if (!idRe.match(id).hasMatch())
        return {};
    QUrl endpoint(QStringLiteral("https://www.youtube.com/oembed"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("url"), QStringLiteral("https://www.youtube.com/watch?v=") + id);
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    endpoint.setQuery(q);
    return endpoint;
}

QVariantMap LinkPreviews::parseOEmbed(const QByteArray &json, const QString &url)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    const QString title = o.value(QStringLiteral("title")).toString().simplified();
    const QString image = o.value(QStringLiteral("thumbnail_url")).toString();
    if (title.isEmpty() && image.isEmpty())
        return {{QStringLiteral("status"), QStringLiteral("none")}, {QStringLiteral("src"), QStringLiteral("oembed")}};
    QVariantMap r{{QStringLiteral("src"), QStringLiteral("oembed")},
                  {QStringLiteral("status"), QStringLiteral("ready")},
                  {QStringLiteral("type"), QStringLiteral("page")},
                  {QStringLiteral("url"), url},
                  {QStringLiteral("title"), title},
                  {QStringLiteral("description"), o.value(QStringLiteral("author_name")).toString().simplified()},
                  {QStringLiteral("image"), image},
                  {QStringLiteral("site"), o.value(QStringLiteral("provider_name")).toString(QUrl(url).host())}};
    const int w = o.value(QStringLiteral("thumbnail_width")).toInt();
    const int h = o.value(QStringLiteral("thumbnail_height")).toInt();
    if (w > 0 && h > 0) {
        r.insert(QStringLiteral("iw"), w);
        r.insert(QStringLiteral("ih"), h);
    }
    return r;
}

QVariantMap LinkPreviews::get(const QString &url, bool fetchPages)
{
    if (const auto it = m_cache.constFind(url); it != m_cache.constEnd())
        return *it;

    if (isImageUrl(url)) {
        QVariantMap r{{QStringLiteral("status"), QStringLiteral("ready")},
                      {QStringLiteral("type"), QStringLiteral("image")},
                      {QStringLiteral("url"), url},
                      {QStringLiteral("image"), url},
                      {QStringLiteral("t"), QDateTime::currentSecsSinceEpoch()}};
        m_cache.insert(url, r);
        return r;
    }
    const QUrl u(url);
    if (!fetchPages || !u.isValid() || (u.scheme() != u"http" && u.scheme() != u"https") || m_inFlight > 8)
        return {{QStringLiteral("status"), QStringLiteral("none")}};

    if (m_cache.size() > kMaxCache)
        m_cache.clear();
    m_cache.insert(url, {{QStringLiteral("status"), QStringLiteral("loading")}});

    const QUrl oembed = oembedUrl(url);
    QNetworkRequest req(oembed.isEmpty() ? u : oembed);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) WeeChatDesktop/0.1 (link preview)"));
    req.setRawHeader("Accept", oembed.isEmpty() ? "text/html,application/xhtml+xml,image/*;q=0.9,*/*;q=0.5"
                                                : "application/json");
    QNetworkReply *reply = m_nam.get(req);
    ++m_inFlight;

    auto buffer = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, buffer, html = oembed.isEmpty()] {
        buffer->append(reply->read(kMaxBytes - buffer->size()));
        if (buffer->size() >= kMaxBytes || (html && buffer->contains("</head>")))
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, buffer, url, oembed] {
        --m_inFlight;
        reply->deleteLater();
        if (!oembed.isEmpty()) {
            if (reply->isOpen())
                buffer->append(reply->readAll().left(kMaxBytes));
            // A network failure is not cached across runs (no "src"), so it is retried next start.
            finish(url, reply->error() == QNetworkReply::NoError ? parseOEmbed(*buffer, url)
                                                                 : QVariantMap{{QStringLiteral("status"), QStringLiteral("none")}});
            return;
        }
        const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        if (reply->isOpen())
            buffer->append(reply->readAll().left(kMaxBytes));
        if (type.startsWith(u"image/")) {
            finish(url, {{QStringLiteral("status"), QStringLiteral("ready")},
                         {QStringLiteral("type"), QStringLiteral("image")},
                         {QStringLiteral("url"), url},
                         {QStringLiteral("image"), reply->url().toString()}});
            return;
        }
        if (!type.isEmpty() && !type.contains(u"html")) {
            finish(url, {{QStringLiteral("status"), QStringLiteral("none")}});
            return;
        }
        finish(url, parseHtml(*buffer, reply->url().toString()));
    });
    return m_cache.value(url);
}

void LinkPreviews::finish(const QString &url, QVariantMap result)
{
    result.insert(QStringLiteral("t"), QDateTime::currentSecsSinceEpoch());
    m_cache.insert(url, result);
    m_saveTimer.start();
    emit updated(url);
}
