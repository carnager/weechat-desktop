import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import WeechatDesktop.Core

// Everything that highlighted you this session, newest first. Click to jump there.
Popup {
    id: panel
    signal jump(string buffer, var lineId)

    parent: Overlay.overlay
    x: parent.width - width - 12
    y: 12
    width: Math.min(420, parent.width - 24)
    height: parent.height - 24
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Theme.alpha("#000000", Theme.dark ? 0.35 : 0.18) }
    background: Rectangle { radius: Theme.radiusLarge; color: Theme.surface; border.width: 1; border.color: Theme.border }
    enter: Transition { NumberAnimation { property: "x"; from: panel.parent.width; to: panel.parent.width - panel.width - 12; duration: Theme.anim; easing.type: Easing.OutCubic } }

    onOpened: { list.forceActiveFocus(); Relay.markMentionsSeen() }

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            Layout.leftMargin: 18
            Layout.rightMargin: 10
            Icon { name: "at"; size: 18; color: Theme.accent }
            Text {
                Layout.fillWidth: true
                text: qsTr("Mentions")
                color: Theme.text
                font.pixelSize: Theme.large
                font.weight: Font.Bold
            }
            IconButton { iconName: "trash"; tip: qsTr("Clear"); enabled: Relay.mentions.length > 0; onClicked: Relay.clearMentions() }
            IconButton { iconName: "x"; tip: qsTr("Close (Esc)"); onClicked: panel.close() }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        ListView {
            acceptedButtons: Qt.NoButton // desktop: no mouse-drag flicking (touch still works)
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Relay.mentions
            currentIndex: 0
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            Keys.onReturnPressed: if (currentItem) currentItem.clicked()
            delegate: AbstractButton {
                id: m
                required property var modelData
                required property int index
                width: ListView.view.width
                topPadding: 10
                bottomPadding: 11
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                onClicked: { panel.jump(modelData.buffer, modelData.lineId); panel.close() }
                background: Rectangle {
                    color: m.hovered || m.ListView.isCurrentItem ? Theme.hover : "transparent"
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
                }
                contentItem: ColumnLayout {
                    id: col
                    spacing: 4
                    RowLayout {
                        Layout.leftMargin: 18
                        Layout.rightMargin: 18
                        spacing: 6
                        Text {
                            text: m.modelData.nick
                            color: Platform.nickColor(m.modelData.nick, Theme.dark)
                            font.weight: Font.Bold
                            font.pixelSize: Theme.small + 1
                        }
                        Text { text: qsTr("in"); color: Theme.faint; font.pixelSize: Theme.small }
                        Text { text: m.modelData.bufferName; color: Theme.muted; font.pixelSize: Theme.small; font.weight: Font.DemiBold }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: Qt.formatDateTime(m.modelData.date, "MMM d, HH:mm")
                            color: Theme.faint
                            font.pixelSize: Theme.tiny
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 18
                        Layout.rightMargin: 18
                        text: m.modelData.text
                        color: Theme.text
                        font.pixelSize: Theme.small + 1
                        wrapMode: Text.Wrap
                        maximumLineCount: 4
                        elide: Text.ElideRight
                    }
                }
            }
            Column {
                anchors.centerIn: parent
                visible: Relay.mentions.length === 0
                spacing: 8
                Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "bell"; size: 32; color: Theme.faint }
                Text { text: qsTr("No mentions yet"); color: Theme.muted; font.pixelSize: Theme.fontSize; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: qsTr("Highlights from any buffer collect here."); color: Theme.faint; font.pixelSize: Theme.small; anchors.horizontalCenter: parent.horizontalCenter }
            }
        }
    }
}
