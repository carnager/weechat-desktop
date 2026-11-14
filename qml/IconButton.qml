import QtQuick
import QtQuick.Controls.Basic

// A flat icon button with hover/pressed states, an optional badge and a tooltip.
AbstractButton {
    id: btn
    property string iconName
    property string tip
    property color iconColor: checked ? Theme.accent : (hovered ? Theme.text : Theme.muted)
    property real iconSize: 18
    property int badge: 0
    property bool badgeAlert: false

    implicitWidth: 32
    implicitHeight: 32
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    background: Rectangle {
        radius: Theme.radius
        color: btn.down ? Theme.pressed : (btn.hovered || btn.checked ? Theme.hover : "transparent")
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
    }
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: btn.iconName
            size: btn.iconSize
            color: btn.iconColor
            opacity: btn.enabled ? 1 : 0.4
        }
    }
    Badge {
        visible: btn.badge > 0
        count: btn.badge
        alert: btn.badgeAlert
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: -3
        anchors.topMargin: -3
        compact: true
    }

    ToolTip.visible: hovered && tip.length > 0
    ToolTip.delay: 600
    ToolTip.text: tip
}
