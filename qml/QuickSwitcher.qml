import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import WeechatDesktop.Core

// Ctrl+K: fuzzy-jump to any buffer. Empty query lists unread and recent buffers first.
Popup {
    id: sw
    property var results: []
    property int current: 0

    function openSwitcher() {
        if (opened) { close(); return }
        field.text = ""
        refresh()
        open()
        field.forceActiveFocus()
    }
    function refresh() {
        results = Relay.switcherResults(field.text, 40)
        current = 0
    }
    function activate(i) {
        if (i >= 0 && i < results.length) {
            Relay.openBuffer(results[i])
            close()
        } else if (field.text.startsWith("#")) {
            Relay.joinChannel(field.text.trim())
            close()
        }
    }

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round(parent.height * 0.14)
    width: Math.min(560, parent.width - 40)
    height: Math.min(440, contentCol.implicitHeight + 20)
    modal: true
    focus: true
    padding: 10
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Theme.alpha("#000000", Theme.dark ? 0.45 : 0.25) }
    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
    }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.anim }
            NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: Theme.anim; easing.type: Easing.OutCubic }
        }
    }

    contentItem: ColumnLayout {
        id: contentCol
        spacing: 8

        AppTextField {
            id: field
            Layout.fillWidth: true
            leadingIcon: "search"
            placeholderText: qsTr("Jump to a buffer… (type #channel to join)")
            implicitHeight: 42
            font.pixelSize: Theme.large
            onTextChanged: sw.refresh()
            Keys.onPressed: (e) => {
                if (e.key === Qt.Key_Down || e.key === Qt.Key_Tab || (e.key === Qt.Key_N && e.modifiers & Qt.ControlModifier)) {
                    sw.current = Math.min(sw.results.length - 1, sw.current + 1); view.positionViewAtIndex(sw.current, ListView.Contain); e.accepted = true
                } else if (e.key === Qt.Key_Up || e.key === Qt.Key_Backtab || (e.key === Qt.Key_P && e.modifiers & Qt.ControlModifier)) {
                    sw.current = Math.max(0, sw.current - 1); view.positionViewAtIndex(sw.current, ListView.Contain); e.accepted = true
                } else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                    sw.activate(sw.current); e.accepted = true
                } else if (e.key === Qt.Key_K && e.modifiers & Qt.ControlModifier) {
                    sw.close(); e.accepted = true
                }
            }
        }

        ListView {
            acceptedButtons: Qt.NoButton // desktop: no mouse-drag flicking (touch still works)
            id: view
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 340)
            clip: true
            model: sw.results
            boundsBehavior: Flickable.StopAtBounds
            delegate: AbstractButton {
                id: item
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 40
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                onHoveredChanged: if (hovered) sw.current = index
                onClicked: sw.activate(index)
                background: Rectangle {
                    radius: Theme.radius
                    color: item.index === sw.current ? Theme.accentSoft : "transparent"
                }
                contentItem: RowLayout {
                    spacing: 10
                    Item { implicitWidth: 4 }
                    Icon {
                        name: item.modelData.kind === "channel" ? "hash" : item.modelData.kind === "private" ? "user"
                            : item.modelData.kind === "server" ? "server" : item.modelData.kind === "core" ? "terminal" : "message"
                        size: 16
                        color: item.index === sw.current ? Theme.accent : Theme.muted
                    }
                    Text {
                        text: item.modelData.name
                        color: Theme.text
                        font.pixelSize: Theme.fontSize
                        font.weight: item.modelData.unread > 0 ? Font.DemiBold : Font.Normal
                    }
                    Text {
                        Layout.fillWidth: true
                        text: item.modelData.server
                        color: Theme.faint
                        font.pixelSize: Theme.small
                        elide: Text.ElideRight
                    }
                    Badge {
                        count: item.modelData.unread
                        alert: item.modelData.highlights > 0 || (item.modelData.kind === "private" && item.modelData.unread > 0)
                    }
                    Text {
                        visible: item.index === sw.current
                        text: "⏎"
                        color: Theme.faint
                        font.pixelSize: Theme.small
                    }
                    Item { implicitWidth: 4 }
                }
            }
        }

        Text {
            visible: sw.results.length === 0
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 6
            text: field.text.startsWith("#") ? qsTr("Press Enter to join %1").arg(field.text.trim()) : qsTr("No matching buffers")
            color: Theme.muted
            font.pixelSize: Theme.small
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 6
            spacing: 14
            Repeater {
                model: [["↑↓", qsTr("navigate")], ["⏎", qsTr("open")], ["esc", qsTr("close")]]
                delegate: Row {
                    required property var modelData
                    spacing: 5
                    Rectangle {
                        width: k.implicitWidth + 8; height: 18; radius: 4
                        color: Theme.raised
                        border.width: 1; border.color: Theme.border
                        Text { id: k; anchors.centerIn: parent; text: modelData[0]; color: Theme.muted; font.pixelSize: Theme.tiny }
                    }
                    Text { text: modelData[1]; color: Theme.faint; font.pixelSize: Theme.tiny; anchors.verticalCenter: parent.verticalCenter }
                }
            }
        }
    }
}
