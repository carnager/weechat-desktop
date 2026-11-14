import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import WeechatDesktop.Core

Rectangle {
    id: sidebar
    signal openSwitcher()
    signal openSettings()
    signal openMentions()
    signal openShortcuts()

    color: Theme.sidebar
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header: app identity + connection state
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 8
            Layout.topMargin: 10
            Layout.preferredHeight: 40
            spacing: 8

            Image {
                source: "qrc:/resources/icon.svg"
                sourceSize: Qt.size(48, 48)
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    text: "WeeChat"
                    color: Theme.text
                    font.pixelSize: Theme.large
                    font.weight: Font.Bold
                }
                RowLayout {
                    spacing: 5
                    Rectangle {
                        width: 7; height: 7; radius: 3.5
                        color: Relay.connected && Relay.synced ? Theme.ok
                             : (Relay.state === 4 ? Theme.danger : Theme.warn)
                    }
                    Text {
                        Layout.fillWidth: true
                        text: Relay.connected && Relay.synced
                              ? (AppSettings.host.replace(/^[a-z]+:\/\//i, "").replace(/\/.*$/, "") + (Relay.weechatVersion ? " · " + Relay.weechatVersion : ""))
                              : Relay.stateText
                        color: Theme.muted
                        font.pixelSize: Theme.tiny
                        elide: Text.ElideRight
                    }
                }
            }
            IconButton {
                iconName: "at"
                tip: qsTr("Mentions (Ctrl+Shift+M)")
                badge: Relay.unseenMentions
                badgeAlert: true
                onClicked: sidebar.openMentions()
            }
            IconButton {
                iconName: "settings"
                tip: qsTr("Settings (Ctrl+,)")
                onClicked: sidebar.openSettings()
            }
        }

        // Jump-to button (opens the quick switcher)
        AbstractButton {
            id: jump
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 8
            Layout.bottomMargin: 6
            implicitHeight: 34
            hoverEnabled: true
            focusPolicy: Qt.NoFocus
            onClicked: sidebar.openSwitcher()
            background: Rectangle {
                radius: Theme.radius
                color: jump.hovered ? Theme.raised : Theme.field
                border.width: 1
                border.color: Theme.border
            }
            contentItem: RowLayout {
                spacing: 8
                Item { width: 2 }
                Icon { name: "search"; size: 15; color: Theme.muted }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Jump to…")
                    color: Theme.muted
                    font.pixelSize: Theme.small
                }
                Rectangle {
                    radius: 4
                    color: Theme.raised
                    border.width: 1
                    border.color: Theme.border
                    implicitWidth: kbd.implicitWidth + 10
                    implicitHeight: kbd.implicitHeight + 4
                    Text {
                        id: kbd
                        anchors.centerIn: parent
                        text: "Ctrl K"
                        color: Theme.faint
                        font.pixelSize: Theme.tiny
                    }
                }
                Item { width: 2 }
            }
        }

        ListView {
            acceptedButtons: Qt.NoButton // desktop: no mouse-drag flicking (touch still works)
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Relay.sidebar
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            bottomMargin: 8
            ScrollBar.vertical: AppScrollBar {}

            section.property: "section"
            section.criteria: ViewSection.FullString
            section.delegate: Item {
                required property string section
                width: ListView.view.width
                height: section === "WeeChat" ? 6 : 30
                Text {
                    visible: parent.section !== "WeeChat"
                    anchors.left: parent.left
                    anchors.leftMargin: 18
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 5
                    text: parent.section.toUpperCase()
                    color: Theme.faint
                    font.pixelSize: Theme.tiny
                    font.weight: Font.Bold
                    font.letterSpacing: 0.8
                }
            }

            delegate: BufferRow {
                width: ListView.view.width
                onContextMenu: (buffer) => rowMenu.popupFor(buffer)
            }

            // Keep the active buffer in view when switching with the keyboard.
            Connections {
                target: Relay
                function onCurrentChanged() {
                    const r = Relay.sidebar.rowOf(Relay.current)
                    if (r >= 0) list.positionViewAtIndex(r, ListView.Contain)
                }
            }
        }

        // Footer: unread summary
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            Layout.leftMargin: 14
            Layout.rightMargin: 8
            Text {
                Layout.fillWidth: true
                text: Relay.totalUnread > 0 ? qsTr("%n unread", "", Relay.totalUnread) : qsTr("All caught up")
                color: Theme.muted
                font.pixelSize: Theme.tiny
            }
            IconButton {
                iconName: "check-check"
                iconSize: 16
                tip: qsTr("Mark all read (Ctrl+Shift+A)")
                enabled: Relay.totalUnread > 0
                onClicked: Relay.markAllRead()
            }
            IconButton {
                iconName: "keyboard"
                iconSize: 16
                tip: qsTr("Keyboard shortcuts (F1)")
                onClicked: sidebar.openShortcuts()
            }
        }
    }

    // The buffer's context menu: a native desktop menu.
    QtObject {
        id: rowMenu
        property var buffer: null
        function popupFor(buf) {
            buffer = buf
            const entries = [
                { section: buf.name },
                { id: "read", text: qsTr("Mark as read"), icon: "mail-mark-read",
                  enabled: buf.unread > 0 || buf.lowActivity > 0 },
                { id: "copy", text: qsTr("Copy name"), icon: "edit-copy" }
            ]
            if (buf.kind === "channel")
                entries.push(buf.joined ? { id: "part", text: qsTr("Leave"), icon: "system-log-out" }
                                        : { id: "join", text: qsTr("Rejoin"), icon: "go-jump" })
            if (buf.kind !== "core")
                entries.push({ separator: true }, { id: "close", text: qsTr("Close buffer"), icon: "window-close" })
            Platform.popupMenu("buffer", entries, null, 0, 0)
        }
        property Connections conn: Connections {
            target: Platform
            function onMenuTriggered(tag, id) {
                const b = rowMenu.buffer
                if (tag !== "buffer" || !b)
                    return
                switch (id) {
                case "read": Relay.markRead(b); break
                case "copy": Platform.copy(b.name); break
                case "part": Relay.command("/part", b); break
                case "join": Relay.command("/join " + b.name, b); break
                case "close": Relay.closeBuffer(b); break
                }
            }
        }
    }
}
