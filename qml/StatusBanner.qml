import QtQuick
import QtQuick.Layouts
import WeechatDesktop.Core

// Shown above the messages while the relay isn't connected.
Rectangle {
    id: banner
    readonly property bool problem: Relay.state !== 2 /* Connected */ || !Relay.synced
    // A quick (re)connect shouldn't flash a banner: show it once a problem lasts, or at once on failure.
    property bool shown: false
    onProblemChanged: { if (problem) graceTimer.restart(); else { graceTimer.stop(); shown = false } }
    Component.onCompleted: if (problem) graceTimer.restart()
    Timer { id: graceTimer; interval: 1200; onTriggered: banner.shown = banner.problem }
    readonly property bool failed: Relay.state === 4
    visible: implicitHeight > 0
    implicitHeight: problem && (shown || failed) && AppSettings.host.length > 0 ? row.implicitHeight + 14 : 0
    clip: true
    color: Relay.state === 4 ? Theme.alpha(Theme.danger, 0.14) : Theme.alpha(Theme.warn, 0.12)
    Behavior on implicitHeight { NumberAnimation { duration: Theme.anim; easing.type: Easing.OutCubic } }

    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 16
        anchors.rightMargin: 12
        spacing: 10
        Icon {
            name: Relay.state === 4 || Relay.state === 0 ? "wifi-off" : "refresh"
            size: 16
            color: Relay.state === 4 ? Theme.danger : Theme.warn
            RotationAnimation on rotation {
                running: Relay.state === 1 || Relay.state === 3
                from: 0; to: 360; duration: 1200; loops: Animation.Infinite
            }
        }
        Text {
            Layout.fillWidth: true
            text: Relay.stateText + (Relay.errorString ? " — " + Relay.errorString : "")
            color: Theme.text
            font.pixelSize: Theme.small
            elide: Text.ElideRight
        }
        AppButton {
            visible: Relay.state === 3 || Relay.state === 4 || Relay.state === 0
            text: Relay.state === 0 ? qsTr("Connect") : qsTr("Retry now")
            implicitHeight: 28
            onClicked: Relay.state === 3 ? Relay.reconnectNow() : Relay.connectToRelay()
        }
    }
}
