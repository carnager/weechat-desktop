import QtQuick

// An unread counter pill: accent for messages, red for highlights/private.
Rectangle {
    id: badge
    property int count: 0
    property bool alert: false
    property bool compact: false

    visible: count > 0
    height: compact ? 16 : 18
    width: Math.max(height, label.implicitWidth + (compact ? 8 : 10))
    radius: height / 2
    color: alert ? Theme.danger : Theme.accent

    Text {
        id: label
        anchors.centerIn: parent
        text: badge.count > 999 ? "999+" : badge.count
        color: badge.alert ? "#ffffff" : Theme.onAccent
        font.pixelSize: badge.compact ? 10 : 11
        font.weight: Font.Bold
        font.features: { "tnum": 1 }
    }
}
