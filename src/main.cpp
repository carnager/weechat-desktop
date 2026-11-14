#include "Buffer.h"
#include "BufferListModel.h"
#include "Completer.h"
#include "EmojiStore.h"
#include "Formatter.h"
#include "LineModel.h"
#include "LinkPreviews.h"
#include "NickModel.h"
#include "Notifier.h"
#include "Session.h"
#include "Settings.h"

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
#include <QMouseEvent>
#include <QMenu>
#include <QCursor>
#include <QQuickItem>
#include <QWheelEvent>
#include <algorithm>
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
    /// [anchor]. [entries] are {id, text, icon (freedesktop name), enabled, checked} or
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
            QAction *a = menu->addAction(QIcon::fromTheme(e.value(QStringLiteral("icon")).toString()),
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

    // Development only (--screenshot-action): real input events, to test scrolling and selection.
    Q_INVOKABLE void devMouse(int type, qreal x, qreal y)
    {
        static const QEvent::Type types[] = {QEvent::MouseButtonPress, QEvent::MouseMove, QEvent::MouseButtonRelease};
        if (QWindow *w = mainWindow()) {
            const Qt::MouseButtons held = type == 2 ? Qt::NoButton : Qt::LeftButton;
            QMouseEvent e(types[std::clamp(type, 0, 2)], QPointF(x, y), w->mapToGlobal(QPointF(x, y)),
                          type == 1 ? Qt::NoButton : Qt::LeftButton, held, Qt::NoModifier);
            QCoreApplication::sendEvent(w, &e);
        }
    }
    Q_INVOKABLE void devWheel(qreal x, qreal y, int angleDeltaY)
    {
        if (QWindow *w = mainWindow()) {
            QWheelEvent e(QPointF(x, y), w->mapToGlobal(QPointF(x, y)), QPoint(), QPoint(0, angleDeltaY),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(w, &e);
        }
    }

signals:
    void menuTriggered(const QString &tag, const QString &id);
    void toggleWindowRequested();
    void raiseRequested();

private:
    static QWindow *mainWindow()
    {
        for (QWindow *w : QGuiApplication::topLevelWindows())
            if (qobject_cast<QQuickWindow *>(w) && w->isVisible())
                return w;
        return nullptr;
    }
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
    QCommandLineOption screenshot(QStringLiteral("screenshot"),
                                  QStringLiteral("Save a screenshot of the window to <file> and quit (for development)."),
                                  QStringLiteral("file"));
    QCommandLineOption delay(QStringLiteral("screenshot-delay"), QStringLiteral("Milliseconds to wait before the screenshot."),
                             QStringLiteral("ms"), QStringLiteral("2500"));
    QCommandLineOption action(QStringLiteral("screenshot-action"),
                              QStringLiteral("UI state to show before the screenshot (development)."), QStringLiteral("name"));
    QCommandLineOption multi(QStringLiteral("new-instance"), QStringLiteral("Don't hand over to a running instance."));
    parser.addOptions({screenshot, delay, action, multi});
    parser.process(app);

    // Single instance: a second launch just raises the first window.
    const QString serverName = QStringLiteral("weechat-desktop-") + qEnvironmentVariable("USER");
    if (!parser.isSet(multi) && !parser.isSet(screenshot)) {
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
    auto *platform = new Platform(notifier, &app);
    QObject::connect(notifier, &Notifier::toggleWindowRequested, platform, &Platform::toggleWindowRequested);
    QObject::connect(notifier, &Notifier::quitRequested, &app, &QCoreApplication::quit);
    QObject::connect(session, &Session::raiseRequested, platform, &Platform::raiseRequested);

    QLocalServer instanceServer;
    if (!parser.isSet(multi) && !parser.isSet(screenshot)) {
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
    engine.setInitialProperties({{QStringLiteral("devAction"), parser.value(action)}});
    engine.loadFromModule("WeechatDesktop", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    // Development: WD_FRAMES=<dir> saves a frame every 100 ms for 4 s (startup smoothness).
    if (const QString dir = qEnvironmentVariable("WD_FRAMES"); !dir.isEmpty()) {
        auto *t = new QTimer(&app);
        auto *n = new int(0);
        QObject::connect(t, &QTimer::timeout, &app, [&engine, t, n, dir] {
            if (auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0)))
                w->grabWindow().save(QStringLiteral("%1/%2.png").arg(dir).arg(*n, 3, 10, QLatin1Char('0')));
            if (++*n >= 40)
                t->stop();
        });
        t->start(100);
    }
    if (parser.isSet(screenshot)) {
        const QString file = parser.value(screenshot);
        QTimer::singleShot(parser.value(delay).toInt(), &app, [&engine, file] {
            if (auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
                const bool ok = w->grabWindow().save(file);
                qInfo("screenshot %s: %s", qPrintable(file), ok ? "saved" : "failed");
                // Other visible windows (e.g. the whois window) go next to it: file-1.png, …
                int n = 0;
                for (QWindow *other : QGuiApplication::topLevelWindows()) {
                    auto *q = qobject_cast<QQuickWindow *>(other);
                    if (q && q != w && q->isVisible()) {
                        QString extra = file;
                        extra.insert(extra.lastIndexOf(u'.') < 0 ? extra.size() : extra.lastIndexOf(u'.'),
                                     QStringLiteral("-%1").arg(++n));
                        q->grabWindow().save(extra);
                        qInfo("screenshot %s: saved", qPrintable(extra));
                    }
                }
                for (QWidget *widget : QApplication::topLevelWidgets()) { // native menus
                    if (!widget->isVisible())
                        continue;
                    QString extra = file;
                    extra.insert(extra.lastIndexOf(u'.') < 0 ? extra.size() : extra.lastIndexOf(u'.'),
                                 QStringLiteral("-w%1").arg(++n));
                    widget->grab().save(extra);
                    qInfo("screenshot %s: saved", qPrintable(extra));
                }
            }
            QCoreApplication::quit();
        });
    }
    if (settings->autoConnect() && settings->hasConnection())
        session->connectToRelay();

    return app.exec();
}

#include "main.moc"
