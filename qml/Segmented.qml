import QtQuick
import QtQuick.Layouts

// A segmented control: options is [{value, label}], value is the selected one.
Rectangle {
    id: seg
    property var options: []
    property string value
    signal picked(string value)

    implicitHeight: 34
    implicitWidth: rowItem.implicitWidth + 6
    radius: Theme.radius
    color: Theme.field
    border.width: 1
    border.color: Theme.border

    Row {
        id: rowItem
        anchors.centerIn: parent
        spacing: 2
        Repeater {
            model: seg.options
            delegate: Rectangle {
                required property var modelData
                readonly property bool selected: modelData.value === seg.value
                width: Math.max(64, label.implicitWidth + 24)
                height: seg.height - 6
                radius: Theme.radius - 2
                color: selected ? Theme.accentSoft : (ma.containsMouse ? Theme.hover : "transparent")
                Behavior on color { ColorAnimation { duration: Theme.animFast } }
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: modelData.label
                    color: selected ? Theme.accent : Theme.muted
                    font.pixelSize: Theme.small
                    font.weight: selected ? Font.DemiBold : Font.Normal
                }
                MouseArea {
                    id: ma
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: seg.picked(modelData.value)
                }
            }
        }
    }
}
