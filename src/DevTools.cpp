#include "DevTools.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>
#include <algorithm>

namespace {
QWindow *mainWindow()
{
    for (QWindow *w : QGuiApplication::topLevelWindows())
        if (qobject_cast<QQuickWindow *>(w) && w->isVisible())
            return w;
    return nullptr;
}

/// "shot.png" → "shot-<suffix>.png"
QString sibling(const QString &file, const QString &suffix)
{
    QString out = file;
    const int dot = out.lastIndexOf(u'.');
    out.insert(dot < 0 ? out.size() : dot, QLatin1Char('-') + suffix);
    return out;
}
} // namespace

DevTools::DevTools(QObject *parent)
    : QObject(parent)
    , m_screenshot(QStringLiteral("screenshot"),
                   QStringLiteral("Save a screenshot of the window to <file> and quit (development)."),
                   QStringLiteral("file"))
    , m_delay(QStringLiteral("screenshot-delay"), QStringLiteral("Milliseconds to wait before the screenshot."),
              QStringLiteral("ms"), QStringLiteral("2500"))
    , m_action(QStringLiteral("screenshot-action"),
               QStringLiteral("UI state to set up before the screenshot (development)."), QStringLiteral("name"))
{
}

void DevTools::addOptions(QCommandLineParser &parser) const
{
    parser.addOptions({m_screenshot, m_delay, m_action});
}

bool DevTools::screenshotMode(const QCommandLineParser &parser) const
{
    return parser.isSet(m_screenshot);
}

QString DevTools::action(const QCommandLineParser &parser) const
{
    return parser.value(m_action);
}

void DevTools::start(const QCommandLineParser &parser, QQmlApplicationEngine &engine)
{
    // WD_FRAMES=<dir>: a frame every 100 ms for 4 s, to check startup doesn't visibly re-layout.
    if (const QString dir = qEnvironmentVariable("WD_FRAMES"); !dir.isEmpty()) {
        auto *timer = new QTimer(this);
        auto frame = std::make_shared<int>(0);
        connect(timer, &QTimer::timeout, this, [&engine, timer, frame, dir] {
            if (auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0)))
                w->grabWindow().save(QStringLiteral("%1/%2.png").arg(dir).arg(*frame, 3, 10, QLatin1Char('0')));
            if (++*frame >= 40)
                timer->stop();
        });
        timer->start(100);
    }

    if (!parser.isSet(m_screenshot))
        return;
    const QString file = parser.value(m_screenshot);
    QTimer::singleShot(parser.value(m_delay).toInt(), this, [&engine, file] {
        if (auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0))) {
            qInfo("screenshot %s: %s", qPrintable(file), w->grabWindow().save(file) ? "saved" : "failed");
            // Other windows (e.g. whois) and open native menus go next to it: shot-1.png, shot-w2.png…
            int n = 0;
            for (QWindow *other : QGuiApplication::topLevelWindows()) {
                auto *q = qobject_cast<QQuickWindow *>(other);
                if (q && q != w && q->isVisible()) {
                    const QString extra = sibling(file, QString::number(++n));
                    q->grabWindow().save(extra);
                    qInfo("screenshot %s: saved", qPrintable(extra));
                }
            }
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (!widget->isVisible())
                    continue;
                const QString extra = sibling(file, QStringLiteral("w%1").arg(++n));
                widget->grab().save(extra);
                qInfo("screenshot %s: saved", qPrintable(extra));
            }
        }
        QCoreApplication::quit();
    });
}

void DevTools::mouse(int type, qreal x, qreal y)
{
    static const QEvent::Type types[] = {QEvent::MouseButtonPress, QEvent::MouseMove, QEvent::MouseButtonRelease};
    QWindow *w = mainWindow();
    if (!w)
        return;
    const Qt::MouseButtons held = type == 2 ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent e(types[std::clamp(type, 0, 2)], QPointF(x, y), w->mapToGlobal(QPointF(x, y)),
                  type == 1 ? Qt::NoButton : Qt::LeftButton, held, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &e);
}

void DevTools::wheel(qreal x, qreal y, int angleDeltaY)
{
    QWindow *w = mainWindow();
    if (!w)
        return;
    QWheelEvent e(QPointF(x, y), w->mapToGlobal(QPointF(x, y)), QPoint(), QPoint(0, angleDeltaY), Qt::NoButton,
                  Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(w, &e);
}
