#pragma once

// Development hooks, compiled only with -DWD_DEVTOOLS=ON (the default for non-Release builds):
// headless screenshots, a startup frame recorder and simulated input, so UI behaviour can be
// checked without a person at the keyboard.

#include <QCommandLineOption>
#include <QObject>

class QCommandLineParser;
class QQmlApplicationEngine;

class DevTools : public QObject
{
    Q_OBJECT
public:
    explicit DevTools(QObject *parent = nullptr);

    void addOptions(QCommandLineParser &parser) const;
    /// Taking a screenshot: the run is throwaway, so it skips the single-instance handover.
    bool screenshotMode(const QCommandLineParser &parser) const;
    /// The UI state Main.qml should set up (--screenshot-action), or empty.
    QString action(const QCommandLineParser &parser) const;
    /// After the QML is loaded: schedule the screenshot, start WD_FRAMES recording.
    void start(const QCommandLineParser &parser, QQmlApplicationEngine &engine);

    /// Real input events, as a person would make them. type: 0 press, 1 move, 2 release.
    Q_INVOKABLE void mouse(int type, qreal x, qreal y);
    /// The pointer moving with no button held.
    Q_INVOKABLE void hover(qreal x, qreal y);
    Q_INVOKABLE void wheel(qreal x, qreal y, int angleDeltaY);
    /// The main window's cursor shape right now (Qt::CursorShape).
    Q_INVOKABLE int cursorShape() const;

private:
    QCommandLineOption m_screenshot;
    QCommandLineOption m_delay;
    QCommandLineOption m_action;
};
