import QtQuick
import QtQuick.Controls.Basic

Button {
    id: btn
    property bool primary: false
    property bool danger: false
    property string iconName

    hoverEnabled: true
    implicitHeight: 36
    leftPadding: 16
    rightPadding: 16
    font.pixelSize: Theme.fontSize
    font.weight: Font.DemiBold

    contentItem: Row {
        spacing: 8
        Icon {
            visible: btn.iconName.length > 0
            name: btn.iconName
            size: 16
            color: label.color
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: label
            text: btn.text
            font: btn.font
            color: btn.primary ? Theme.onAccent : (btn.danger ? Theme.danger : Theme.text)
            anchors.verticalCenter: parent.verticalCenter
            opacity: btn.enabled ? 1 : 0.5
        }
    }
    background: Rectangle {
        radius: Theme.radius
        color: btn.primary ? (btn.down ? Qt.darker(Theme.accent, 1.15) : btn.hovered ? Qt.lighter(Theme.accent, 1.06) : Theme.accent)
                           : (btn.down ? Theme.pressed : btn.hovered ? Theme.hover : "transparent")
        border.width: btn.primary ? 0 : 1
        border.color: btn.danger ? Theme.alpha(Theme.danger, 0.5) : Theme.border
        opacity: btn.enabled ? 1 : 0.6
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            border.width: 2
            border.color: Theme.alpha(Theme.accent, 0.5)
            visible: btn.visualFocus
        }
    }
}
