import QtQuick
import QtQuick.Controls.Basic

// A slim scrollbar, always shown when there's something to scroll; it widens and darkens on hover.
ScrollBar {
    id: bar
    policy: ScrollBar.AsNeeded
    hoverEnabled: true
    minimumSize: 0.04
    padding: 3
    // Wider than it looks, so it's easy to grab.
    implicitWidth: 14
    contentItem: Rectangle {
        implicitWidth: bar.hovered || bar.pressed ? 8 : 6
        radius: width / 2
        color: bar.pressed ? Theme.muted : Theme.borderStrong
        opacity: bar.size >= 1.0 ? 0 : bar.hovered || bar.pressed || bar.active ? 1.0 : 0.6
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Behavior on implicitWidth { NumberAnimation { duration: 120 } }
    }
}
