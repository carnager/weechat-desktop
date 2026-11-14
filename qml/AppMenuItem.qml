import QtQuick
import QtQuick.Controls.Basic

MenuItem {
    id: item
    property string iconName
    property bool danger: false
    property string shortcutText

    implicitHeight: visible ? 32 : 0
    hoverEnabled: true
    leftPadding: 10
    rightPadding: 10

    contentItem: Row {
        spacing: 10
        Icon {
            name: item.iconName
            visible: item.iconName.length > 0
            size: 15
            color: item.danger ? Theme.danger : (item.highlighted ? Theme.text : Theme.muted)
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: item.text
            color: item.danger ? Theme.danger : Theme.text
            font.pixelSize: Theme.small + 1
            anchors.verticalCenter: parent.verticalCenter
            opacity: item.enabled ? 1 : 0.45
        }
    }
    Text {
        visible: item.shortcutText.length > 0
        text: item.shortcutText
        color: Theme.faint
        font.pixelSize: Theme.tiny
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
    }
    background: Rectangle {
        radius: Theme.radius - 2
        color: item.highlighted ? Theme.hover : "transparent"
    }
}
