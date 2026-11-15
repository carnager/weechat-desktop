import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: dlg
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(720, parent.width - 40)
    height: Math.min(620, parent.height - 40)
    modal: true
    focus: true
    padding: 24
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Theme.alpha("#000000", Theme.dark ? 0.45 : 0.25) }
    background: Rectangle { radius: Theme.radiusLarge; color: Theme.surface; border.width: 1; border.color: Theme.border }

    readonly property var groups: [
        { title: qsTr("Navigation"), keys: [
            [["Ctrl", "K"], qsTr("Jump to buffer")],
            [["Alt", "1", "…", "0"], qsTr("Buffer 1 to 10 in the sidebar")],
            [["Alt", "↑", "or", "↓"], qsTr("Previous / next buffer")],
            [["Ctrl", "Tab"], qsTr("Next buffer")],
            [["Alt", "A"], qsTr("Next unread (highlights first)")],
            [["Alt", "←"], qsTr("Last visited buffer")],
            [["Ctrl", "Shift", "M"], qsTr("Mentions")],
            [["Ctrl", "F"], qsTr("Search in buffer")],
            [["Ctrl", "O"], qsTr("Open a link on screen by its label")],
            [["Ctrl", "Shift", "O"], qsTr("Links in this buffer")] ] },
        { title: qsTr("Input"), keys: [
            [["Enter"], qsTr("Send")],
            [["Shift", "Enter"], qsTr("New line")],
            [["Tab"], qsTr("Complete nick, command, channel")],
            [[":sm"], qsTr("Emoji suggestions")],
            [["@", "or", "#", "or", "/"], qsTr("Suggest nicks, channels or commands")],
            [["↑", "or", "↓"], qsTr("Input history")],
            [["Ctrl", "E"], qsTr("Emoji picker")],
            [["Esc"], qsTr("Close popups · jump to bottom")] ] },
        { title: qsTr("Reading"), keys: [
            [["Page Up", "or", "Page Down"], qsTr("Scroll messages")],
            [["Ctrl", "Home", "or", "End"], qsTr("Oldest / newest (empty input)")],
            [["Ctrl", "Shift", "A"], qsTr("Mark everything read")],
            [["Middle click"], qsTr("Mark a buffer read")] ] },
        { title: qsTr("Window"), keys: [
            [["Ctrl", "B"], qsTr("Toggle sidebar")],
            [["Ctrl", "Shift", "N"], qsTr("Toggle member list")],
            [["Ctrl", "+", "or", "-", "or", "0"], qsTr("Text size")],
            [["Ctrl", "Shift", "D"], qsTr("Toggle dark / light")],
            [["Ctrl", ","], qsTr("Settings")],
            [["F11"], qsTr("Full screen")],
            [["Ctrl", "Shift", "R"], qsTr("Reconnect now")],
            [["Ctrl", "Q"], qsTr("Quit")] ] }
    ]

    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Icon { name: "keyboard"; size: 20; color: Theme.accent }
            Text { Layout.fillWidth: true; text: qsTr("Keyboard shortcuts"); color: Theme.text; font.pixelSize: Theme.large; font.weight: Font.Bold }
            IconButton { iconName: "x"; onClicked: dlg.close() }
        }
        ScrollView {
            id: sv
            // Desktop: wheel and scrollbar scroll; a mouse drag selects instead of flicking.
            Binding { target: sv.contentItem; property: "acceptedButtons"; value: Qt.NoButton; when: sv.contentItem instanceof Flickable }
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            GridLayout {
                width: sv.availableWidth
                columns: width > 560 ? 2 : 1
                columnSpacing: 28
                rowSpacing: 18
                Repeater {
                    model: dlg.groups
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: 6
                        Text { text: modelData.title.toUpperCase(); color: Theme.faint; font.pixelSize: Theme.tiny; font.weight: Font.Bold; font.letterSpacing: 0.8 }
                        Repeater {
                            model: modelData.keys
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 10
                                Text { Layout.fillWidth: true; text: modelData[1]; color: Theme.text; font.pixelSize: Theme.small + 1; wrapMode: Text.Wrap }
                                // Each entry is a key cap; "or" and "…" are separators, drawn as plain text so
                                // they can't be mistaken for keys (the "/" key has a cap of its own).
                                Row {
                                    spacing: 3
                                    Repeater {
                                        model: modelData[0]
                                        delegate: Item {
                                            id: cap
                                            required property string modelData
                                            readonly property bool separator: modelData === "or" || modelData === "…"
                                            width: separator ? sep.implicitWidth + 4 : Math.max(22, kt.implicitWidth + 10)
                                            height: 22
                                            Rectangle {
                                                anchors.fill: parent
                                                visible: !cap.separator
                                                radius: 5
                                                color: Theme.raised
                                                border.width: 1; border.color: Theme.border
                                                Text { id: kt; anchors.centerIn: parent; text: cap.modelData; color: Theme.muted; font.pixelSize: Theme.tiny; font.weight: Font.DemiBold }
                                            }
                                            Text {
                                                id: sep
                                                anchors.centerIn: parent
                                                visible: cap.separator
                                                text: cap.modelData === "or" ? qsTr("or") : "…"
                                                color: Theme.faint
                                                font.pixelSize: Theme.tiny
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
