import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A settings row: title, optional description and a switch on the right.
AbstractButton {
    id: row
    property string description
    checkable: true
    hoverEnabled: true
    implicitHeight: Math.max(40, col.implicitHeight + 14)
    Layout.fillWidth: true

    background: Rectangle {
        radius: Theme.radius
        color: row.hovered ? Theme.hover : "transparent"
        border.width: row.visualFocus ? 2 : 0
        border.color: Theme.alpha(Theme.accent, 0.5)
    }
    contentItem: Item {
        Column {
            id: col
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.right: knob.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text {
                text: row.text
                color: Theme.text
                font.pixelSize: Theme.fontSize
                width: parent.width
                wrapMode: Text.Wrap
            }
            Text {
                visible: row.description.length > 0
                text: row.description
                color: Theme.muted
                font.pixelSize: Theme.small
                width: parent.width
                wrapMode: Text.Wrap
            }
        }
        Rectangle {
            id: knob
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            width: 38
            height: 22
            radius: 11
            color: row.checked ? Theme.accent : Theme.borderStrong
            Behavior on color { ColorAnimation { duration: Theme.anim } }
            Rectangle {
                width: 16
                height: 16
                radius: 8
                y: 3
                x: row.checked ? parent.width - width - 3 : 3
                color: "#ffffff"
                Behavior on x { NumberAnimation { duration: Theme.anim; easing.type: Easing.OutCubic } }
            }
        }
    }
}
