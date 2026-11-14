import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import WeechatDesktop.Core

ApplicationWindow {
    id: win
    width: 1240
    height: 800
    minimumWidth: 640
    minimumHeight: 420
    visible: true
    color: Theme.window
    title: {
        let t = "WeeChat Desktop"
        if (Relay.current)
            t = Relay.current.name + " — " + t
        if (Relay.totalHighlights > 0)
            t = "(" + Relay.totalHighlights + "!) " + t
        else if (Relay.totalUnread > 0)
            t = "(" + Relay.totalUnread + ") " + t
        return t
    }

    font.pixelSize: Theme.fontSize

    Settings {
        category: "window"
        property alias width: win.width
        property alias height: win.height
    }

    // The connect screen: first run, "Edit connection", or the first attempt this session failed.
    readonly property bool showConnect: !AppSettings.hasConnection() || connectRequested
                                        || (Relay.state === 4 && !everSynced)
    property bool connectRequested: false
    property bool everSynced: false
    Connections {
        target: Relay
        function onSyncedChanged() { if (Relay.synced) win.everSynced = true }
    }
    readonly property bool narrow: width < 900

    // Development: put the UI into a given state for screenshots (--screenshot-action).
    property string devAction
    Timer { id: sendTimer; interval: 400; onTriggered: chat.sendInput() }
    Timer { id: pasteTimer; interval: 300; onTriggered: chat.sendInput() } // Enter once: should ask
    Timer { id: pasteTimer2; interval: 700; onTriggered: chat.sendInput() } // and again: sends
    // "select": drag from one message to another, a few lines down, like a person would.
    Timer {
        id: selectTimer
        interval: 30
        repeat: true
        property int step: 0
        onTriggered: {
            const x0 = 900, y0 = 282, x1 = 270, y1 = 577
            if (step === 0) Platform.devMouse(0, x0, y0)
            else if (step <= 10) Platform.devMouse(1, x0 + (x1 - x0) * step / 10, y0 + (y1 - y0) * step / 10)
            else { Platform.devMouse(2, x1, y1); stop(); console.log("selected:", JSON.stringify(chat.selectedText())) }
            ++step
        }
    }
    Timer { id: queryTimer; interval: 1200; onTriggered: Relay.sendInput("hi alice, testing the desktop client :wave:") }
    Timer {
        running: win.devAction.length > 0
        interval: 1200
        onTriggered: {
            const a = win.devAction
            if (a === "switcher") switcher.openSwitcher()
            else if (a === "emoji") chat.openEmojiPicker()
            else if (a === "settings") settingsDialog.open()
            else if (a === "shortcuts") shortcuts.open()
            else if (a === "mentions") mentions.open()
            else if (a === "light") AppSettings.theme = "light"
            else if (a === "compact") AppSettings.layout = "compact"
            else if (a === "completion") chat.insertText(":sm")
            else if (a === "nicks") chat.insertText("@a")
            else if (a === "unread") Relay.openNextUnread()
            else if (a === "query") { Relay.openQuery("alice"); queryTimer.start() }
            else if (a === "scroll") { for (let i = 0; i < 6; ++i) Platform.devWheel(800, 400, 120) }
            else if (a === "select") selectTimer.start()
            else if (a.startsWith("whois:")) whoisWindow.showFor(a.substring(6))
            else if (a === "nickmenu") nickMenu.popupFor("alice", null)
            else if (a === "pastesend") { chat.insertText("first pasted line\nsecond pasted line\nthird pasted line"); pasteTimer.start(); pasteTimer2.start() }
            else if (a === "paste") { chat.insertText("first pasted line\nsecond pasted line\nthird pasted line"); pasteTimer.start() }
            else if (a === "scrollsend") { chat.scrollBy(-3000); chat.insertText("sent from the input bar"); sendTimer.start() }
            else if (a === "send") { Relay.sendInput("trying the new client :tada: works great"); Relay.sendInput("/me waves") }
        }
    }

    onActiveChanged: Relay.windowActive = active

    Component.onCompleted: {
        Relay.windowActive = active
        updateFormatting()
        Platform.setTray(AppSettings.closeToTray)
    }

    function updateFormatting() {
        Relay.refreshFormatting(Theme.dark, Theme.accent.toString(), Theme.link.toString())
    }
    Connections {
        target: Theme
        function onDarkChanged() { win.updateFormatting() }
        function onAccentChanged() { win.updateFormatting() }
    }
    Connections {
        target: AppSettings
        function onCloseToTrayChanged() { Platform.setTray(AppSettings.closeToTray) }
    }
    Connections {
        target: Relay
        function onTotalsChanged() { Platform.setUnread(Relay.totalUnread, Relay.totalHighlights) }
        function onToast(message) { toast.show(message) }
        function onUntrustedCertificateChanged() {
            if (Object.keys(Relay.untrustedCertificate).length > 0)
                certDialog.open()
        }
    }
    Connections {
        target: Platform
        function onRaiseRequested() { win.bringToFront() }
        function onToggleWindowRequested() {
            if (win.visible && win.active) win.hide()
            else win.bringToFront()
        }
    }

    function bringToFront() {
        win.show()
        win.raise()
        win.requestActivate()
    }

    onClosing: (close) => {
        if (AppSettings.closeToTray && Platform.trayAvailable) {
            close.accepted = false
            win.hide()
        } else {
            Qt.quit()
        }
    }

    // ------------------------------------------------------------------ layout

    Loader {
        anchors.fill: parent
        active: win.showConnect
        sourceComponent: ConnectView {
            cancellable: win.connectRequested && win.everSynced
            onDone: win.connectRequested = false
            onCancelled: win.connectRequested = false
        }
    }

    Item {
        id: main
        anchors.fill: parent
        visible: !win.showConnect

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Sidebar {
                id: sidebar
                Layout.fillHeight: true
                Layout.preferredWidth: AppSettings.showSidebar && !win.narrow ? AppSettings.sidebarWidth : 0
                visible: Layout.preferredWidth > 0
                onOpenSwitcher: switcher.openSwitcher()
                onOpenSettings: settingsDialog.open()
                onOpenMentions: mentions.open()
                onOpenShortcuts: shortcuts.open()
            }
            Rectangle { // drag handle between sidebar and chat
                visible: sidebar.visible
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                color: Theme.border
                MouseArea {
                    anchors.fill: parent
                    anchors.leftMargin: -3
                    anchors.rightMargin: -3
                    cursorShape: Qt.SplitHCursor
                    property real startX
                    property int startWidth
                    onPressed: (m) => { startX = mapToItem(main, m.x, 0).x; startWidth = AppSettings.sidebarWidth }
                    onPositionChanged: (m) => {
                        const x = mapToItem(main, m.x, 0).x
                        AppSettings.sidebarWidth = Math.max(180, Math.min(460, startWidth + x - startX))
                    }
                }
            }

            ChatView {
                id: chat
                Layout.fillWidth: true
                Layout.fillHeight: true
                onOpenSwitcher: switcher.openSwitcher()
                onOpenMentions: mentions.open()
                onOpenSettings: settingsDialog.open()
                onNickClicked: (nick, item) => nickMenu.popupFor(nick, item)
            }

            Rectangle {
                visible: nicklist.visible
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                color: Theme.border
                MouseArea {
                    anchors.fill: parent
                    anchors.leftMargin: -3
                    anchors.rightMargin: -3
                    cursorShape: Qt.SplitHCursor
                    property real startX
                    property int startWidth
                    onPressed: (m) => { startX = mapToItem(main, m.x, 0).x; startWidth = AppSettings.nicklistWidth }
                    onPositionChanged: (m) => {
                        const x = mapToItem(main, m.x, 0).x
                        AppSettings.nicklistWidth = Math.max(150, Math.min(400, startWidth - (x - startX)))
                    }
                }
            }
            NickList {
                id: nicklist
                Layout.fillHeight: true
                Layout.preferredWidth: AppSettings.nicklistWidth
                visible: AppSettings.showNicklist && !!Relay.current && Relay.current.kind === "channel"
                         && win.width > 760
                buffer: Relay.current
                onNickClicked: (nick, item) => nickMenu.popupFor(nick, item)
                onNickActivated: (nick) => Relay.openQuery(nick)
            }
        }
    }

    // ------------------------------------------------------------------ overlays

    QuickSwitcher { id: switcher }
    SettingsDialog {
        id: settingsDialog
        onEditConnection: { close(); win.connectRequested = true }
    }
    ShortcutsDialog { id: shortcuts }
    CertificateDialog { id: certDialog }
    MentionsPanel {
        id: mentions
        onJump: (buffer, lineId) => { Relay.openBufferByName(buffer); chat.scrollToLine(lineId) }
    }
    NickMenu {
        id: nickMenu
        onMention: (nick) => chat.insertText(nick + (chat.inputEmpty ? ": " : " "))
        onInfo: (nick) => whoisWindow.showFor(nick)
    }
    WhoisWindow {
        id: whoisWindow
        transientParent: win
        onMention: (nick) => chat.insertText(nick + (chat.inputEmpty ? ": " : " "))
    }
    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 96
        z: 100
    }

    // ------------------------------------------------------------------ keyboard

    Shortcut { sequences: ["Ctrl+K", "Ctrl+T"]; onActivated: switcher.openSwitcher() }
    Shortcut { sequence: "Ctrl+,"; onActivated: settingsDialog.open() }
    Shortcut { sequences: ["F1", "Ctrl+/", "Ctrl+?"]; onActivated: shortcuts.open() }
    Shortcut { sequences: ["Alt+Down", "Ctrl+PgDown", "Ctrl+Tab"]; onActivated: Relay.openRelative(1) }
    Shortcut { sequences: ["Alt+Up", "Ctrl+PgUp", "Ctrl+Shift+Tab", "Ctrl+Backtab"]; onActivated: Relay.openRelative(-1) }
    Shortcut { sequences: ["Alt+A", "Ctrl+Shift+U"]; onActivated: if (!Relay.openNextUnread()) toast.show(qsTr("No unread buffers")) }
    Shortcut { sequences: ["Alt+Left", "Alt+<"]; onActivated: Relay.openPrevious() }
    Shortcut { sequence: "Ctrl+Shift+M"; onActivated: mentions.opened ? mentions.close() : mentions.open() }
    Shortcut { sequence: "Ctrl+B"; onActivated: AppSettings.showSidebar = !AppSettings.showSidebar }
    Shortcut { sequences: ["Ctrl+Shift+N", "Ctrl+Shift+L"]; onActivated: AppSettings.showNicklist = !AppSettings.showNicklist }
    Shortcut { sequence: "Ctrl+F"; onActivated: chat.openSearch() }
    Shortcut { sequence: "Ctrl+E"; onActivated: chat.openEmojiPicker() }
    Shortcut { sequence: "Ctrl+Shift+A"; onActivated: { Relay.markAllRead(); toast.show(qsTr("All buffers marked read")) } }
    Shortcut { sequence: "Ctrl+Shift+R"; onActivated: Relay.reconnectNow() }
    Shortcut { sequences: ["Ctrl+=", "Ctrl++"]; onActivated: AppSettings.fontSize = Math.min(24, AppSettings.fontSize + 1) }
    Shortcut { sequence: "Ctrl+-"; onActivated: AppSettings.fontSize = Math.max(10, AppSettings.fontSize - 1) }
    Shortcut { sequence: "Ctrl+0"; onActivated: AppSettings.fontSize = 14 }
    Shortcut { sequence: "F11"; onActivated: win.visibility === Window.FullScreen ? win.showNormal() : win.showFullScreen() }
    Shortcut { sequence: "Ctrl+Q"; onActivated: Qt.quit() }
    Shortcut { sequence: "Ctrl+W"; onActivated: if (AppSettings.closeToTray) win.hide() }
    Shortcut { sequence: "Ctrl+Shift+D"; onActivated: AppSettings.theme = Theme.dark ? "light" : "dark" }

    // Alt+1…9, Alt+0 = 10th
    Repeater {
        model: 10
        delegate: Item {
            required property int index
            Shortcut {
                sequence: "Alt+" + ((index + 1) % 10)
                onActivated: Relay.openBufferAt(index + 1)
            }
        }
    }
}
