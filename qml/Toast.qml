import QtQuick

// Transient bottom-centre message.
Rectangle {
    id: toast
    function show(message) {
        label.text = message
        opacity = 1
        hideTimer.restart()
    }

    width: Math.min(label.implicitWidth + 32, parent.width - 40)
    height: label.implicitHeight + 18
    radius: height / 2
    color: Theme.dark ? "#e6e8ee" : "#1d2129"
    opacity: 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: Theme.anim } }

    Text {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, toast.parent ? toast.parent.width - 72 : implicitWidth)
        elide: Text.ElideRight
        color: Theme.dark ? "#14161b" : "#ffffff"
        font.pixelSize: Theme.small
        font.weight: Font.Medium
    }
    Timer {
        id: hideTimer
        interval: 3500
        onTriggered: toast.opacity = 0
    }
}
