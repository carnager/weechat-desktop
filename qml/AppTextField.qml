import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: field
    property string leadingIcon

    color: Theme.text
    placeholderTextColor: Theme.faint
    selectionColor: Theme.alpha(Theme.accent, 0.4)
    selectedTextColor: Theme.text
    font.pixelSize: Theme.fontSize
    leftPadding: leadingIcon ? 34 : 12
    rightPadding: 12
    topPadding: 8
    bottomPadding: 8
    implicitHeight: 38
    selectByMouse: true

    background: Rectangle {
        radius: Theme.radius
        color: Theme.field
        border.width: field.activeFocus ? 2 : 1
        border.color: field.activeFocus ? Theme.accent : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }
        Icon {
            visible: field.leadingIcon.length > 0
            name: field.leadingIcon
            size: 16
            color: Theme.muted
            anchors.left: parent.left
            anchors.leftMargin: 11
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
