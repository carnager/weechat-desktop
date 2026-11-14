#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSslCertificate>
#include <QTimer>
#include <QVariantMap>
#include <QWebSocket>

/// The WebSocket to WeeChat's `relay api`: authentication, TLS with optional certificate pinning,
/// reconnecting with backoff and a ping watchdog for half-open sockets. Knows nothing about what
/// the messages mean — that is Session's job.
class RelayConnection : public QObject
{
    Q_OBJECT
public:
    enum State { Disconnected, Connecting, Connected, Reconnecting, Failed };
    Q_ENUM(State)

    struct Params {
        QString host;
        int port = 9000;
        bool tls = false;
        QString password;
        QString pinnedSha256;
    };

    explicit RelayConnection(QObject *parent = nullptr);

    State state() const { return m_state; }
    QString errorString() const { return m_error; }
    /// Details of a TLS certificate that was refused, for the "trust this certificate?" dialog.
    QVariantMap untrustedCertificate() const { return m_untrusted; }

    void open(const Params &params);
    void close();
    /// Retry now, skipping any pending backoff (e.g. after the network came back).
    void reconnectNow();
    bool send(const QJsonObject &message);

    /// Normalise a host the user typed: drop a scheme, path or port. Empty if unusable.
    static QString normalizeHost(const QString &input);
    static QString fingerprint(const QSslCertificate &cert);
    static QStringList subprotocols(const QString &password);

signals:
    void stateChanged();
    void opened();
    void messageReceived(const QJsonObject &message);
    void untrustedCertificateChanged();

private:
    void setState(State s);
    void startSocket();
    QString describeError(QAbstractSocket::SocketError code, const QString &raw) const;
    void scheduleReconnect();
    void fail(const QString &message);
    void onSslErrors(const QList<QSslError> &errors);

    QWebSocket *m_socket = nullptr;
    Params m_params;
    State m_state = Disconnected;
    QString m_error;
    QVariantMap m_untrusted;
    bool m_certRejected = false;
    bool m_wanted = false;
    int m_attempt = 0;
    QTimer m_reconnectTimer;
    QTimer m_pingTimer;
    qint64 m_lastActivity = 0;
};
