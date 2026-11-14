#include "RelayConnection.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QUrl>
#include <QWebSocketHandshakeOptions>

namespace {
// WD_DEBUG_RELAY=1 logs every frame (truncated) to stderr.
const bool kDebug = qEnvironmentVariableIntValue("WD_DEBUG_RELAY") != 0;
constexpr int kPingIntervalMs = 20'000;
constexpr int kActivityTimeoutMs = 65'000; // > 3 pings; any frame resets it
constexpr int kMaxBackoffMs = 30'000;
}

RelayConnection::RelayConnection(QObject *parent)
    : QObject(parent)
{
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &RelayConnection::startSocket);

    m_pingTimer.setInterval(kPingIntervalMs);
    connect(&m_pingTimer, &QTimer::timeout, this, [this] {
        if (m_state != Connected)
            return;
        // Half-open detection: a live socket answers pings. Prolonged silence means it's dead.
        if (QDateTime::currentMSecsSinceEpoch() - m_lastActivity > kActivityTimeoutMs) {
            m_error = tr("The relay stopped responding");
            scheduleReconnect();
            return;
        }
        send(QJsonObject{{QStringLiteral("request"), QStringLiteral("POST /api/ping")}});
    });
}

QString RelayConnection::normalizeHost(const QString &input)
{
    QString s = input.trimmed();
    if (const int i = s.indexOf(u"://"); i >= 0)
        s = s.mid(i + 3);
    s = s.section(QLatin1Char('/'), 0, 0);
    if (s.isEmpty())
        return {};
    for (const QChar c : s)
        if (c.isSpace())
            return {};
    if (s.startsWith(u'[')) {
        const int end = s.indexOf(u']');
        return end > 0 ? s.left(end + 1) : QString();
    }
    if (s.count(u':') == 1)
        s = s.section(QLatin1Char(':'), 0, 0);
    return s;
}

QString RelayConnection::fingerprint(const QSslCertificate &cert)
{
    const QByteArray d = cert.digest(QCryptographicHash::Sha256).toHex().toUpper();
    QStringList parts;
    for (int i = 0; i < d.size(); i += 2)
        parts << QString::fromLatin1(d.mid(i, 2));
    return parts.join(QLatin1Char(':'));
}

QStringList RelayConnection::subprotocols(const QString &password)
{
    // The relay api accepts the password as a bearer token in the subprotocol list, which works
    // for any WebSocket client (browsers can't set an Authorization header).
    const QByteArray token = QByteArray("plain:" + password.toUtf8())
                                 .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return {QStringLiteral("api.weechat"),
            QStringLiteral("base64url.bearer.authorization.weechat.") + QString::fromLatin1(token)};
}

void RelayConnection::open(const Params &params)
{
    close();
    m_params = params;
    m_wanted = true;
    m_attempt = 0;
    m_untrusted.clear();
    emit untrustedCertificateChanged();
    setState(Connecting);
    startSocket();
}

void RelayConnection::close()
{
    m_wanted = false;
    m_reconnectTimer.stop();
    m_pingTimer.stop();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_error.clear();
    setState(Disconnected);
}

void RelayConnection::reconnectNow()
{
    if (!m_wanted || m_state == Connected || m_state == Connecting)
        return;
    m_reconnectTimer.stop();
    m_attempt = 0;
    startSocket();
}

bool RelayConnection::send(const QJsonObject &message)
{
    if (!m_socket || m_state != Connected)
        return false;
    const QString text = QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
    if (kDebug)
        qInfo("relay >> %s", qPrintable(text.left(400)));
    const qint64 n = m_socket->sendTextMessage(text);
    return n > 0;
}

void RelayConnection::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
}

void RelayConnection::startSocket()
{
    if (!m_wanted)
        return;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
    }

    const QString host = normalizeHost(m_params.host);
    if (host.isEmpty()) {
        fail(tr("Invalid host \"%1\"").arg(m_params.host));
        return;
    }

    QUrl url;
    url.setScheme(m_params.tls ? QStringLiteral("wss") : QStringLiteral("ws"));
    url.setHost(host.startsWith(u'[') ? host.mid(1, host.size() - 2) : host);
    url.setPort(m_params.port);
    url.setPath(QStringLiteral("/api"));

    m_certRejected = false;
    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_socket, &QWebSocket::connected, this, [this] {
        m_attempt = 0;
        m_error.clear();
        m_lastActivity = QDateTime::currentMSecsSinceEpoch();
        setState(Connected);
        m_pingTimer.start();
        emit opened();
    });
    connect(m_socket, &QWebSocket::textMessageReceived, this, [this](const QString &text) {
        m_lastActivity = QDateTime::currentMSecsSinceEpoch();
        if (kDebug)
            qInfo("relay << %s", qPrintable(text.left(600)));
        QJsonParseError err;
        const auto doc = QJsonDocument::fromJson(text.toUtf8(), &err);
        if (doc.isObject())
            emit messageReceived(doc.object());
    });
    connect(m_socket, &QWebSocket::pong, this, [this] { m_lastActivity = QDateTime::currentMSecsSinceEpoch(); });
    connect(m_socket, &QWebSocket::sslErrors, this, &RelayConnection::onSslErrors);
    connect(m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError code) {
        if (!m_socket)
            return;
        const QString err = m_socket->errorString();
        if (kDebug)
            qInfo("relay error: %s", qPrintable(err));
        if (m_certRejected) {
            fail(tr("The relay's TLS certificate isn't trusted"));
            return;
        }
        // An HTTP 401/403 during the handshake means the password is wrong: retrying won't help.
        // (Qt reports a 401 as "Unsupported WWW-Authenticate challenges".)
        if (err.contains(u"401") || err.contains(u"403") || err.contains(u"WWW-Authenticate")
            || err.contains(u"Unauthorized", Qt::CaseInsensitive)) {
            fail(tr("The relay refused the password"));
            return;
        }
        m_error = describeError(code, err);
    });
    connect(m_socket, &QWebSocket::disconnected, this, [this] {
        if (kDebug && m_socket)
            qInfo("relay disconnected: code %d, reason \"%s\"", int(m_socket->closeCode()),
                  qPrintable(m_socket->closeReason()));
        if (!m_wanted || m_state == Failed)
            return;
        if (m_error.isEmpty())
            m_error = m_socket && !m_socket->closeReason().isEmpty() ? m_socket->closeReason()
                                                                      : tr("Connection lost");
        scheduleReconnect();
    });

    if (kDebug)
        qInfo("relay connecting to %s", qPrintable(url.toString()));
    QWebSocketHandshakeOptions options;
    options.setSubprotocols(subprotocols(m_params.password));
    QNetworkRequest req(url);
    req.setTransferTimeout(20'000);
    m_socket->open(req, options);
}

QString RelayConnection::describeError(QAbstractSocket::SocketError code, const QString &raw) const
{
    const QString host = normalizeHost(m_params.host);
    // Checked first: Qt reports this with ConnectionRefusedError. Qt says "processHandshake: Unknown error condition" for any non-101 HTTP answer: usually
    // plain ws:// talking to an HTTPS port, or a proxy that doesn't forward /api.
    if (raw.contains(u"processHandshake") || raw.contains(u"handshake", Qt::CaseInsensitive)) {
        if (!m_params.tls)
            return tr("The server answered, but not as a WeeChat relay. If it's behind HTTPS "
                      "(a reverse proxy, port 443), turn on TLS.");
        return tr("The server answered, but not as a WeeChat relay. Is the api relay reachable "
                  "at /api on this host and port?");
    }
    switch (code) {
    case QAbstractSocket::HostNotFoundError:
        return tr("Can't find the host \"%1\". Check the spelling.").arg(host);
    case QAbstractSocket::ConnectionRefusedError:
        return tr("Nothing is listening on %1, port %2.").arg(host).arg(m_params.port);
    case QAbstractSocket::SocketTimeoutError:
        return tr("%1 didn't answer on port %2 (timed out).").arg(host).arg(m_params.port);
    case QAbstractSocket::SslHandshakeFailedError:
        return tr("The TLS handshake failed. Is this port really TLS? (%1)").arg(raw);
    default:
        break;
    }
    if (code == QAbstractSocket::RemoteHostClosedError && !m_params.tls)
        return tr("The server closed the connection. If it expects TLS, turn on TLS.");
    return raw;
}

void RelayConnection::onSslErrors(const QList<QSslError> &errors)
{
    const QSslCertificate leaf = m_socket->sslConfiguration().peerCertificate();
    const QString fp = fingerprint(leaf);
    if (!m_params.pinnedSha256.isEmpty() && fp.compare(m_params.pinnedSha256, Qt::CaseInsensitive) == 0) {
        m_socket->ignoreSslErrors(errors);
        return;
    }
    QStringList reasons;
    for (const auto &e : errors)
        reasons << e.errorString();
    m_untrusted = {
        {QStringLiteral("host"), m_params.host},
        {QStringLiteral("port"), m_params.port},
        {QStringLiteral("sha256"), fp},
        {QStringLiteral("subject"), leaf.subjectDisplayName()},
        {QStringLiteral("issuer"), leaf.issuerDisplayName()},
        {QStringLiteral("notAfter"), leaf.expiryDate().toLocalTime().toString(Qt::TextDate)},
        {QStringLiteral("reasons"), reasons.join(QStringLiteral("; "))},
        {QStringLiteral("replacesPinned"), !m_params.pinnedSha256.isEmpty()},
    };
    m_certRejected = true;
    emit untrustedCertificateChanged();
}

void RelayConnection::scheduleReconnect()
{
    m_pingTimer.stop();
    if (!m_wanted || m_state == Failed)
        return;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    ++m_attempt;
    const int delay = std::min(kMaxBackoffMs, 1000 * (1 << std::min(m_attempt - 1, 5)));
    setState(Reconnecting);
    m_reconnectTimer.start(delay);
}

void RelayConnection::fail(const QString &message)
{
    m_reconnectTimer.stop();
    m_pingTimer.stop();
    m_error = message;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_state = Failed;
    emit stateChanged();
}
