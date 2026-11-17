import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import WeechatDesktop.Core

Popup {
    id: dlg
    signal editConnection()

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(680, parent.width - 40)
    height: Math.min(640, parent.height - 40)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Theme.alpha("#000000", Theme.dark ? 0.45 : 0.25) }
    background: Rectangle { radius: Theme.radiusLarge; color: Theme.surface; border.width: 1; border.color: Theme.border }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.anim }
            NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: Theme.anim; easing.type: Easing.OutCubic }
        }
    }

    property int page: 0
    readonly property var pages: [
        { icon: "image", label: qsTr("Appearance") },
        { icon: "message", label: qsTr("Messages") },
        { icon: "bell", label: qsTr("Notifications") },
        { icon: "server", label: qsTr("Connection") }
    ]

    component SectionTitle: Text {
        color: Theme.faint
        font.pixelSize: Theme.tiny
        font.weight: Font.Bold
        font.letterSpacing: 0.8
        Layout.topMargin: 10
        Layout.leftMargin: 10
    }
    component Row2: RowLayout {
        property string label
        property string description
        default property alias control: holder.data
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        Layout.minimumHeight: 42
        spacing: 12
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text { text: parent.parent.label; color: Theme.text; font.pixelSize: Theme.fontSize }
            Text {
                visible: text.length > 0
                text: parent.parent.description
                color: Theme.muted
                font.pixelSize: Theme.small
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
        }
        Item {
            id: holder
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }
    }

    contentItem: RowLayout {
        spacing: 0

        // Page list
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 190
            color: Theme.dark ? "#1d2027" : Theme.raised
            radius: Theme.radiusLarge
            Rectangle { anchors.right: parent.right; width: Theme.radiusLarge; height: parent.height; color: parent.color }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 2
                Text {
                    text: qsTr("Settings")
                    color: Theme.text
                    font.pixelSize: Theme.large
                    font.weight: Font.Bold
                    Layout.margins: 8
                    Layout.bottomMargin: 10
                }
                Repeater {
                    model: dlg.pages
                    delegate: AbstractButton {
                        id: tab
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        implicitHeight: 34
                        hoverEnabled: true
                        onClicked: dlg.page = index
                        background: Rectangle {
                            radius: Theme.radius
                            color: dlg.page === tab.index ? Theme.accentSoft : (tab.hovered ? Theme.hover : "transparent")
                        }
                        contentItem: RowLayout {
                            spacing: 10
                            Item { implicitWidth: 2 }
                            Icon { name: tab.modelData.icon; size: 16; color: dlg.page === tab.index ? Theme.accent : Theme.muted }
                            Text {
                                Layout.fillWidth: true
                                text: tab.modelData.label
                                color: dlg.page === tab.index ? Theme.text : Theme.muted
                                font.pixelSize: Theme.fontSize
                                font.weight: dlg.page === tab.index ? Font.DemiBold : Font.Normal
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                Text {
                    text: "WeeChat Desktop " + Qt.application.version
                    color: Theme.faint
                    font.pixelSize: Theme.tiny
                    Layout.margins: 8
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                Layout.leftMargin: 22
                Layout.rightMargin: 10
                Text {
                    Layout.fillWidth: true
                    text: dlg.pages[dlg.page].label
                    color: Theme.text
                    font.pixelSize: Theme.large
                    font.weight: Font.Bold
                }
                IconButton { iconName: "x"; tip: qsTr("Close (Esc)"); onClicked: dlg.close() }
            }

            ScrollView {
                id: scroll
                // Desktop: wheel and scrollbar scroll; a mouse drag selects instead of flicking.
                Binding { target: scroll.contentItem; property: "acceptedButtons"; value: Qt.NoButton; when: scroll.contentItem instanceof Flickable }
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true

                StackLayout {
                    width: scroll.availableWidth - 24
                    x: 12
                    currentIndex: dlg.page

                    // ---------------- Appearance
                    ColumnLayout {
                        spacing: 4
                        Row2 {
                            label: qsTr("Theme")
                            Segmented {
                                options: [{ value: "system", label: qsTr("System") }, { value: "dark", label: qsTr("Dark") }, { value: "light", label: qsTr("Light") }]
                                value: AppSettings.theme
                                onPicked: (v) => AppSettings.theme = v
                            }
                        }
                        Row2 {
                            label: qsTr("Accent colour")
                            Row {
                                spacing: 6
                                Repeater {
                                    model: Theme.accents
                                    delegate: Rectangle {
                                        required property string modelData
                                        width: 22; height: 22; radius: 11
                                        color: modelData
                                        border.width: AppSettings.accent === modelData ? 2 : 0
                                        border.color: Theme.text
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: AppSettings.accent = parent.modelData
                                        }
                                    }
                                }
                            }
                        }
                        Row2 {
                            label: qsTr("Message layout")
                            description: qsTr("Cozy groups messages under avatars; Compact is classic IRC lines.")
                            Segmented {
                                options: [{ value: "cozy", label: qsTr("Cozy") }, { value: "compact", label: qsTr("Compact") }]
                                value: AppSettings.layout
                                onPicked: (v) => AppSettings.layout = v
                            }
                        }
                        Row2 {
                            label: qsTr("Query layout")
                            description: qsTr("Private conversations can use a different layout.")
                            Segmented {
                                options: [{ value: "", label: qsTr("Same") }, { value: "cozy", label: qsTr("Cozy") },
                                          { value: "compact", label: qsTr("Compact") }]
                                value: AppSettings.queryLayout
                                onPicked: (v) => AppSettings.queryLayout = v
                            }
                        }
                        Row2 {
                            label: qsTr("Text size")
                            description: qsTr("Ctrl+= / Ctrl+- / Ctrl+0 work anywhere.")
                            RowLayout {
                                spacing: 8
                                Slider {
                                    id: sizeSlider
                                    from: 10; to: 22; stepSize: 1
                                    value: AppSettings.fontSize
                                    onMoved: AppSettings.fontSize = value
                                    implicitWidth: 160
                                    background: Rectangle {
                                        x: sizeSlider.leftPadding
                                        y: sizeSlider.topPadding + sizeSlider.availableHeight / 2 - height / 2
                                        width: sizeSlider.availableWidth; height: 4; radius: 2
                                        color: Theme.border
                                        Rectangle { width: sizeSlider.visualPosition * parent.width; height: parent.height; radius: 2; color: Theme.accent }
                                    }
                                    handle: Rectangle {
                                        x: sizeSlider.leftPadding + sizeSlider.visualPosition * (sizeSlider.availableWidth - width)
                                        y: sizeSlider.topPadding + sizeSlider.availableHeight / 2 - height / 2
                                        width: 16; height: 16; radius: 8
                                        color: "#ffffff"
                                        border.width: 1; border.color: Theme.borderStrong
                                    }
                                }
                                Text { text: AppSettings.fontSize + " px"; color: Theme.muted; font.pixelSize: Theme.small }
                            }
                        }
                        AppSwitch { text: qsTr("24-hour clock"); checked: AppSettings.use24h; onToggled: AppSettings.use24h = checked }
                        AppSwitch {
                            text: qsTr("Show colours from IRC")
                            description: qsTr("mIRC/WeeChat colour codes in messages and topics.")
                            checked: AppSettings.renderColors
                            onToggled: AppSettings.renderColors = checked
                        }
                        AppSwitch {
                            text: qsTr("Hide special buffers")
                            description: qsTr("WeeChat core, plugin and script buffers in the sidebar.")
                            checked: AppSettings.hideSpecialBuffers
                            onToggled: AppSettings.hideSpecialBuffers = checked
                        }
                    }

                    // ---------------- Messages
                    ColumnLayout {
                        spacing: 4
                        AppSwitch {
                            text: qsTr("Show joins, parts and quits")
                            checked: AppSettings.showJoinPart
                            onToggled: AppSettings.showJoinPart = checked
                        }
                        AppSwitch {
                            text: qsTr("Emoji shortcodes")
                            description: qsTr(":tada: becomes 🎉 — in messages you receive and in what you send.")
                            checked: AppSettings.emojiShortcodes
                            onToggled: AppSettings.emojiShortcodes = checked
                        }
                        AppSwitch {
                            text: qsTr("Link previews")
                            description: qsTr("Fetches the page title and image for links. The site sees your IP address.")
                            checked: AppSettings.linkPreviews
                            onToggled: AppSettings.linkPreviews = checked
                        }
                        AppSwitch {
                            text: qsTr("Inline images")
                            description: qsTr("Shows links to images and GIFs inline (GIFs play on hover). The image's host sees your IP address.")
                            checked: AppSettings.inlineImages
                            onToggled: AppSettings.inlineImages = checked
                        }
                        Row2 {
                            Layout.topMargin: 6
                            label: qsTr("Now playing (/np)")
                            description: qsTr("Only this player is asked, so nothing else you play ends up in a channel.")
                            AppButton {
                                id: npButton
                                text: AppSettings.npPlayer ? NowPlaying.playerName(AppSettings.npPlayer) : qsTr("None")
                                onClicked: {
                                    const entries = [{ id: "", text: qsTr("None"), checked: !AppSettings.npPlayer }]
                                    const players = NowPlaying.players()
                                    if (players.length)
                                        entries.push({ separator: true })
                                    for (const p of players)
                                        entries.push({ id: p.key, text: p.name, checked: p.key === AppSettings.npPlayer })
                                    if (!players.length)
                                        entries.push({ id: "-", text: qsTr("No players running"), enabled: false })
                                    Platform.popupMenu("npplayer", entries, npButton, 0, npButton.height + 2)
                                }
                                Connections {
                                    target: Platform
                                    function onMenuTriggered(tag, id) { if (tag === "npplayer") AppSettings.npPlayer = id }
                                }
                            }
                        }
                        ColumnLayout {
                            visible: AppSettings.npPlayer.length > 0
                            Layout.fillWidth: true
                            Layout.leftMargin: 10
                            Layout.rightMargin: 10
                            Layout.bottomMargin: 6
                            spacing: 6
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("What /np sends. {track} is “Artist – Title” (just the title if there's no artist); also {artist}, {title}, {album} and {player}.")
                                color: Theme.muted
                                font.pixelSize: Theme.small
                                wrapMode: Text.Wrap
                            }
                            AppTextField {
                                id: npFormatField
                                Layout.fillWidth: true
                                implicitHeight: 34
                                text: AppSettings.npFormat
                                onEditingFinished: AppSettings.npFormat = text.trim() || "/me np: {track}"
                                Connections {
                                    target: AppSettings
                                    function onNpFormatChanged() { if (!npFormatField.activeFocus) npFormatField.text = AppSettings.npFormat }
                                }
                            }
                        }
                    }

                    // ---------------- Notifications
                    ColumnLayout {
                        spacing: 4
                        AppSwitch {
                            text: qsTr("Desktop notifications")
                            description: qsTr("For highlights and private messages. Click one to open the buffer.")
                            checked: AppSettings.notifications
                            onToggled: AppSettings.notifications = checked
                        }
                        AppSwitch {
                            text: qsTr("Notify even while I'm looking")
                            description: qsTr("Also notify for the buffer that's open in a focused window.")
                            checked: AppSettings.notifyWhenFocused
                            enabled: AppSettings.notifications
                            onToggled: AppSettings.notifyWhenFocused = checked
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 10
                            Layout.rightMargin: 10
                            Layout.topMargin: 6
                            Layout.bottomMargin: 6
                            spacing: 6
                            Text { text: qsTr("Muted nicks"); color: Theme.text; font.pixelSize: Theme.fontSize }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Their highlights don't show in Mentions, count as unread mentions or notify. Separate with spaces; * matches anything (e.g. *Serv).")
                                color: Theme.muted
                                font.pixelSize: Theme.small
                                wrapMode: Text.Wrap
                            }
                            AppTextField {
                                id: mutedField
                                Layout.fillWidth: true
                                implicitHeight: 34
                                text: AppSettings.mutedNicks
                                onEditingFinished: AppSettings.mutedNicks = text.trim()
                                Connections {
                                    target: AppSettings
                                    function onMutedNicksChanged() { if (!mutedField.activeFocus) mutedField.text = AppSettings.mutedNicks }
                                }
                            }
                        }
                        AppSwitch {
                            text: qsTr("Keep running in the tray")
                            description: Platform.trayAvailable ? qsTr("Closing the window hides it; quit from the tray menu or with Ctrl+Q.")
                                                                : qsTr("No system tray was found in this session.")
                            checked: AppSettings.closeToTray
                            enabled: Platform.trayAvailable
                            onToggled: AppSettings.closeToTray = checked
                        }
                    }

                    // ---------------- Connection
                    ColumnLayout {
                        spacing: 4
                        Row2 {
                            label: AppSettings.host.replace(/^[a-z]+:\/\//i, "").replace(/\/.*$/, "") + ":" + AppSettings.port
                            description: (AppSettings.tls ? qsTr("TLS") : qsTr("Plain WebSocket (no TLS)"))
                                         + " · " + Relay.stateText
                                         + (Relay.weechatVersion ? " · WeeChat " + Relay.weechatVersion : "")
                            AppButton { text: qsTr("Edit…"); onClicked: dlg.editConnection() }
                        }
                        AppSwitch {
                            text: qsTr("Connect on startup")
                            checked: AppSettings.autoConnect
                            onToggled: AppSettings.autoConnect = checked
                        }
                        Row2 {
                            visible: AppSettings.pinnedSha256.length > 0
                            label: qsTr("Trusted certificate")
                            description: AppSettings.pinnedSha256
                            AppButton { text: qsTr("Forget"); danger: true; onClicked: AppSettings.pinnedSha256 = "" }
                        }
                        RowLayout {
                            Layout.leftMargin: 10
                            Layout.topMargin: 8
                            spacing: 8
                            AppButton {
                                text: Relay.connected ? qsTr("Disconnect") : qsTr("Connect")
                                iconName: Relay.connected ? "log-out" : "refresh"
                                onClicked: Relay.connected ? Relay.disconnectFromRelay() : Relay.connectToRelay()
                            }
                            AppButton {
                                visible: Relay.connected
                                text: qsTr("Reconnect")
                                iconName: "refresh"
                                onClicked: { Relay.disconnectFromRelay(); Relay.connectToRelay() }
                            }
                        }
                    }
                }
            }
        }
    }
}
