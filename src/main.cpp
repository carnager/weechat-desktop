#include "Buffer.h"
#include "BufferListModel.h"
#include "Completer.h"
#include "EmojiStore.h"
#include "Formatter.h"
#include "LineModel.h"
#include "LinkPreviews.h"
#include "NickModel.h"
#include "Notifier.h"
#include "NowPlaying.h"
#include "Session.h"
#include "Settings.h"
#ifdef WD_DEVTOOLS
#include "DevTools.h"
#endif

#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QQmlApplicationEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QStandardPaths>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QMenu>
#include <QPalette>
#include <QPainter>
#include <QSvgRenderer>
#include <QCursor>
#include <QQuickItem>
#include <qqml.h>

/// Small platform helpers QML has no API for.
class Platform : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT)
public:
    explicit Platform(Notifier *n, QObject *parent = nullptr) : QObject(parent), m_notifier(n) {}
    Q_INVOKABLE void copy(const QString &text) { QGuiApplication::clipboard()->setText(text); }
    /// The primary selection (middle-click paste) on X11 and Wayland; a no-op elsewhere.
    Q_INVOKABLE void copySelection(const QString &text)
    {
        if (QGuiApplication::clipboard()->supportsSelection())
            QGuiApplication::clipboard()->setText(text, QClipboard::Selection);
    }
    Q_INVOKABLE void setTray(bool visible) { m_notifier->setTrayVisible(visible); }
    Q_INVOKABLE void setUnread(int unread, int highlights) { m_notifier->setUnread(unread, highlights); }
    Q_INVOKABLE QString nickColor(const QString &nick, bool dark) const { return Formatter::nickColor(nick, dark).name(); }
    Q_INVOKABLE QString escape(const QString &text) const { return Formatter::escape(text); }
    bool trayAvailable() const { return m_notifier->trayAvailable(); }
    Q_INVOKABLE void quit() { QCoreApplication::quit(); }

    /// A real desktop menu (QMenu: platform style, can extend past the window) at ([x], [y]) in
    /// [anchor]. [entries] are {id, text, svg (an Icon.qml path set), enabled, checked} or
    /// {separator: true} or {section: "title"}. The choice comes back as menuTriggered(tag, id).
    Q_INVOKABLE void popupMenu(const QString &tag, const QVariantList &entries, QQuickItem *anchor, qreal x, qreal y)
    {
        auto *menu = new QMenu();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        for (const QVariant &v : entries) {
            const QVariantMap e = v.toMap();
            if (e.value(QStringLiteral("separator")).toBool()) {
                menu->addSeparator();
                continue;
            }
            if (e.contains(QStringLiteral("section"))) {
                menu->addSection(e.value(QStringLiteral("section")).toString());
                continue;
            }
            QAction *a = menu->addAction(menuIcon(e.value(QStringLiteral("svg")).toString(), menu->palette()),
                                         e.value(QStringLiteral("text")).toString());
            a->setData(e.value(QStringLiteral("id")));
            a->setEnabled(e.value(QStringLiteral("enabled"), true).toBool());
            if (e.contains(QStringLiteral("checked"))) {
                a->setCheckable(true);
                a->setChecked(e.value(QStringLiteral("checked")).toBool());
            }
        }
        connect(menu, &QMenu::triggered, this, [this, tag](QAction *a) { emit menuTriggered(tag, a->data().toString()); });
        connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
        QPoint global = QCursor::pos();
        if (anchor)
            global = anchor->mapToGlobal(QPointF(x, y)).toPoint();
        // Wayland places popups relative to a parent surface: give the menu one.
        QWindow *parent = anchor && anchor->window() ? anchor->window() : QGuiApplication::focusWindow();
        menu->winId();
        if (menu->windowHandle() && parent)
            menu->windowHandle()->setTransientParent(parent);
        menu->popup(global);
    }

private:
    /// The app's own stroke icon (Icon.qml) in the menu's text colours, so menus match the
    /// rest of the window instead of mixing whatever the icon theme has.
    static QIcon menuIcon(const QString &paths, const QPalette &palette)
    {
        if (paths.isEmpty())
            return {};
        QIcon icon;
        const auto add = [&](QIcon::Mode mode, const QColor &color) {
            const QByteArray svg = QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"%1\" "
                "stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">%2</svg>")
                .arg(color.name(), paths).toUtf8();
            QSvgRenderer renderer(svg);
            for (int px : {16, 32, 48}) {
                QPixmap pm(px, px);
                pm.fill(Qt::transparent);
                QPainter p(&pm);
                p.setRenderHint(QPainter::Antialiasing);
                // A little inset: stroke icons drawn edge to edge look heavier than theme icons.
                const qreal m = px / 16.0;
                renderer.render(&p, QRectF(m, m, px - 2 * m, px - 2 * m));
                p.end();
                icon.addPixmap(pm, mode);
            }
        };
        add(QIcon::Normal, palette.color(QPalette::Active, QPalette::WindowText));
        add(QIcon::Active, palette.color(QPalette::Active, QPalette::HighlightedText));
        add(QIcon::Disabled, palette.color(QPalette::Disabled, QPalette::WindowText));
        return icon;
    }

signals:
    void menuTriggered(const QString &tag, const QString &id);
    void toggleWindowRequested();
    void raiseRequested();

private:
    Notifier *m_notifier;
};

/// Network access for QML images (inline previews): a real User-Agent — some sites, Wikimedia
/// among them, refuse Qt's default — and a disk cache so images aren't refetched every session.
class UserAgentNam : public QNetworkAccessManager
{
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *data) override
    {
        QNetworkRequest r(request);
        r.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) WeeChatDesktop/" WD_VERSION));
        r.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
        return QNetworkAccessManager::createRequest(op, r, data);
    }
};

class NamFactory : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *parent) override
    {
        auto *nam = new UserAgentNam(parent);
        auto *cache = new QNetworkDiskCache(nam);
        cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/images"));
        cache->setMaximumCacheSize(200 * 1024 * 1024);
        nam->setCache(cache);
        nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
        return nam;
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("weechat-desktop"));
    QApplication::setApplicationName(QStringLiteral("weechat-desktop"));
    QApplication::setApplicationDisplayName(QStringLiteral("WeeChat Desktop"));
    QApplication::setApplicationVersion(QStringLiteral(WD_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("weechat-desktop"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/resources/icon.svg")));
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("A desktop client for WeeChat's relay api"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption multi(QStringLiteral("new-instance"), QStringLiteral("Don't hand over to a running instance."));
    parser.addOption(multi);
#ifdef WD_DEVTOOLS
    DevTools dev;
    dev.addOptions(parser);
#endif
    parser.process(app);
#ifdef WD_DEVTOOLS
    const bool throwaway = parser.isSet(multi) || dev.screenshotMode(parser);
#else
    const bool throwaway = parser.isSet(multi);
#endif

    // Single instance: a second launch just raises the first window.
    const QString serverName = QStringLiteral("weechat-desktop-") + qEnvironmentVariable("USER");
    if (!throwaway) {
        QLocalSocket probe;
        probe.connectToServer(serverName);
        if (probe.waitForConnected(300)) {
            probe.write("raise\n");
            probe.waitForBytesWritten(300);
            return 0;
        }
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    auto *settings = new Settings(&app);
    auto *emoji = new EmojiStore(&app);
    emoji->loadFromFile(QStringLiteral(":/resources/emoji.json"));
    Formatter::setEmojiLookup([emoji](const QString &code) { return emoji->lookup(code); });
    auto *notifier = new Notifier(&app);
    auto *session = new Session(settings, notifier, &app);
    auto *previews = new LinkPreviews(&app);
    previews->setCacheFile(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                           + QStringLiteral("/previews.json"));
    auto *platform = new Platform(notifier, &app);
    QObject::connect(notifier, &Notifier::toggleWindowRequested, platform, &Platform::toggleWindowRequested);
    QObject::connect(notifier, &Notifier::quitRequested, &app, &QCoreApplication::quit);
    QObject::connect(session, &Session::raiseRequested, platform, &Platform::raiseRequested);

    QLocalServer instanceServer;
    if (!throwaway) {
        QLocalServer::removeServer(serverName);
        instanceServer.listen(serverName);
        QObject::connect(&instanceServer, &QLocalServer::newConnection, platform, [&instanceServer, platform] {
            while (QLocalSocket *s = instanceServer.nextPendingConnection()) {
                QObject::connect(s, &QLocalSocket::readyRead, s, [s, platform] {
                    s->readAll();
                    emit platform->raiseRequested();
                    s->deleteLater();
                });
            }
        });
    }

    qmlRegisterSingletonInstance("WeechatDesktop.Core", 1, 0, "Relay", session);
    qmlRegisterSingletonInstance("WeechatDesktop.Core", 1, 0, "AppSettings", settings);
    qmlRegisterSingletonInstance("WeechatDesktop.Core", 1, 0, "Emoji", emoji);
    qmlRegisterSingletonInstance("WeechatDesktop.Core", 1, 0, "Previews", previews);
    qmlRegisterSingletonInstance("WeechatDesktop.Core", 1, 0, "Platform", platform);
    qmlRegisterSingletonInstance("WeechatDesktop.Core", 1, 0, "NowPlaying", session->nowPlaying());
    qmlRegisterUncreatableType<Buffer>("WeechatDesktop.Core", 1, 0, "Buffer", QStringLiteral("from the relay"));
    qmlRegisterUncreatableType<LineModel>("WeechatDesktop.Core", 1, 0, "LineModel", QStringLiteral("from a buffer"));
    qmlRegisterUncreatableType<NickModel>("WeechatDesktop.Core", 1, 0, "NickModel", QStringLiteral("from a buffer"));
    qmlRegisterUncreatableType<BufferListModel>("WeechatDesktop.Core", 1, 0, "BufferListModel", QStringLiteral("from Relay"));
    qmlRegisterUncreatableType<BufferFilterModel>("WeechatDesktop.Core", 1, 0, "BufferFilterModel", QStringLiteral("from Relay"));
    qmlRegisterUncreatableType<Completer>("WeechatDesktop.Core", 1, 0, "Completer", QStringLiteral("from Relay"));

    QQmlApplicationEngine engine;
    NamFactory namFactory;
    engine.setNetworkAccessManagerFactory(&namFactory);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
#ifdef WD_DEVTOOLS
    qmlRegisterSingletonInstance("WeechatDesktop.Core", 1, 0, "Dev", &dev);
    engine.setInitialProperties({{QStringLiteral("devAction"), dev.action(parser)}});
#endif
    engine.loadFromModule("WeechatDesktop", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

#ifdef WD_DEVTOOLS
    dev.start(parser, engine);
#endif
    if (settings->autoConnect() && settings->hasConnection())
        session->connectToRelay();

    return app.exec();
}

#include "main.moc"
