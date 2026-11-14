import QtQuick
import QtQuick.Controls.Basic

Menu {
    id: menu
    padding: 5
    implicitWidth: 220
    delegate: AppMenuItem {}
    background: Rectangle {
        implicitWidth: 220
        radius: Theme.radius + 2
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
        layer.enabled: false
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animFast } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animFast } }
}
