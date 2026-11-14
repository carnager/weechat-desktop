import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import WeechatDesktop.Core

AbstractButton {
    id: row
    required property var buffer
    required property string name
    required property string kind
    required property int unread
    required property int highlights
    required property int low
    required property bool active
    required property bool joined
    required property int number
    required property int index
    signal contextMenu(var buffer)

    readonly property bool alert: highlights > 0 || (kind === "private" && unread > 0)
    readonly property bool hasUnread: unread > 0

    implicitHeight: 32
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    onClicked: Relay.openBuffer(buffer)

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: row.contextMenu(row.buffer)
    }
    TapHandler {
        acceptedButtons: Qt.MiddleButton
        onTapped: Relay.markRead(row.buffer)
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            radius: Theme.radius
            color: row.active ? Theme.accentSoft : (row.down ? Theme.pressed : row.hovered ? Theme.hover : "transparent")
            Behavior on color { ColorAnimation { duration: Theme.animFast } }
        }
        Rectangle { // unread pip on the left edge
            visible: row.hasUnread && !row.active
            width: 4; height: 8; radius: 2
            x: 2
            anchors.verticalCenter: parent.verticalCenter
            color: row.alert ? Theme.danger : Theme.text
        }
    }

    contentItem: RowLayout {
        spacing: 8
        Item { implicitWidth: 10 }
        Icon {
            name: row.kind === "channel" ? "hash" : row.kind === "private" ? "user"
                : row.kind === "server" ? "server" : row.kind === "core" ? "terminal" : "message"
            size: 15
            color: row.active ? Theme.accent : (row.hasUnread ? Theme.text : Theme.faint)
            opacity: row.joined ? 1 : 0.5
        }
        Text {
            Layout.fillWidth: true
            text: row.kind === "channel" ? row.name.replace(/^#/, "") : row.name
            color: row.active ? Theme.text : (row.hasUnread ? Theme.text : (row.low > 0 ? Theme.muted : Theme.faint))
            font.pixelSize: Theme.fontSize - 0.5
            font.weight: row.hasUnread || row.active ? Font.DemiBold : Font.Normal
            font.italic: !row.joined
            elide: Text.ElideRight
        }
        Badge {
            count: row.alert ? Math.max(row.highlights, row.unread) : row.unread
            alert: row.alert
            visible: count > 0 && (!row.active)
        }
        Text {
            visible: row.index < 10 && row.hovered && !(row.unread > 0 && !row.active)
            text: "Alt " + ((row.index + 1) % 10)
            color: Theme.faint
            font.pixelSize: Theme.tiny
        }
        Item { implicitWidth: 12 }
    }
}
