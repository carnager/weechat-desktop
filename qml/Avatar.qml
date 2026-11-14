import QtQuick

// A coloured circle with a nick's initial, standing in for a profile picture.
Rectangle {
    id: avatar
    property string nick
    property color tint: Theme.accent
    property real size: 36

    width: size
    height: size
    radius: size / 2
    color: Theme.alpha(tint, Theme.dark ? 0.22 : 0.18)
    border.width: 1
    border.color: Theme.alpha(tint, 0.35)

    Text {
        anchors.centerIn: parent
        text: {
            const n = avatar.nick.replace(/^[^A-Za-z0-9À-￿]+/, "")
            return n.length ? n.charAt(0).toUpperCase() : "?"
        }
        color: avatar.tint
        font.pixelSize: avatar.size * 0.44
        font.weight: Font.DemiBold
    }
}
