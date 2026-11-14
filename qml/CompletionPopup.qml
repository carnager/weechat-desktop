import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Suggestions for :emoji:, /commands, @nicks and #channels, shown above the input.
Popup {
    id: popup
    property var items: []
    property string kind
    property int currentIndex: 0
    signal picked(var item)

    function move(d) {
        if (items.length === 0) return
        currentIndex = (currentIndex + d + items.length) % items.length
        view.positionViewAtIndex(currentIndex, ListView.Contain)
    }

    padding: 5
    height: Math.min(view.contentHeight, 8 * 34) + header.height + 12
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    focus: false

    background: Rectangle {
        radius: Theme.radius + 2
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        spacing: 2
        Text {
            id: header
            Layout.leftMargin: 8
            Layout.topMargin: 2
            text: popup.kind === "emoji" ? qsTr("EMOJI") : popup.kind === "command" ? qsTr("COMMANDS")
                : popup.kind === "channel" ? qsTr("CHANNELS") : qsTr("PEOPLE")
            color: Theme.faint
            font.pixelSize: Theme.tiny - 1
            font.weight: Font.Bold
            font.letterSpacing: 0.8
        }
        ListView {
            acceptedButtons: Qt.NoButton // desktop: no mouse-drag flicking (touch still works)
            id: view
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 8 * 34)
            clip: true
            model: popup.items
            boundsBehavior: Flickable.StopAtBounds
            delegate: AbstractButton {
                id: entry
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 34
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                onHoveredChanged: if (hovered) popup.currentIndex = index
                onClicked: popup.picked(modelData)
                background: Rectangle {
                    radius: Theme.radius - 2
                    color: entry.index === popup.currentIndex ? Theme.accentSoft : "transparent"
                }
                contentItem: RowLayout {
                    spacing: 10
                    Item { implicitWidth: 2 }
                    Text {
                        visible: !!entry.modelData.icon
                        text: entry.modelData.icon || ""
                        font.pixelSize: 20
                    }
                    Rectangle {
                        visible: !!entry.modelData.color
                        width: 8; height: 8; radius: 4
                        color: entry.modelData.color || "transparent"
                    }
                    Text {
                        text: entry.modelData.label
                        color: entry.index === popup.currentIndex ? Theme.text : Theme.text
                        font.pixelSize: Theme.small + 1
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: entry.modelData.detail || ""
                        color: Theme.muted
                        font.pixelSize: Theme.small
                        elide: Text.ElideRight
                    }
                    Text {
                        visible: entry.index === popup.currentIndex
                        text: "↹ / ⏎"
                        color: Theme.faint
                        font.pixelSize: Theme.tiny
                    }
                    Item { implicitWidth: 4 }
                }
            }
        }
    }
}
