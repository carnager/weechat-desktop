import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import WeechatDesktop.Core

Item {
    id: bar
    property var buffer: null
    readonly property alias text: area.text
    signal pageScroll(int direction)
    signal jumpEdge(bool bottom)
    signal escapePressed()
    /// Ctrl+C with nothing selected here: copy the chat's selection instead.
    signal copyFallback()
    /// A message or command went out: the chat jumps to the bottom to show it.
    signal sent()
    signal openSwitcher()

    property var previousBuffer: null
    property bool settingText: false
    property var ctx: ({ kind: "" })
    /// Several lines are about to go out as separate messages: Enter again to send, Esc to cancel.
    property bool confirming: false
    readonly property var lines: area.text.split("\n").filter(l => l.trim().length > 0)

    implicitHeight: box.implicitHeight + (confirming ? confirmRow.implicitHeight + 6 : hint.shown ? hint.implicitHeight : 0) + 22

    function focusInput() { area.forceActiveFocus() }
    function insert(t) {
        area.insert(area.cursorPosition, t)
        area.forceActiveFocus()
    }
    function openEmoji() { emojiPicker.openPicker() }
    function setText(t, cursor) {
        settingText = true
        area.text = t
        area.cursorPosition = cursor === undefined ? t.length : cursor
        settingText = false
    }

    function joinLines() {
        setText(lines.map(l => l.trim()).join(" "))
        confirming = false
        area.forceActiveFocus()
    }

    onBufferChanged: {
        confirming = false
        if (previousBuffer)
            previousBuffer.draft = area.text
        setText(buffer ? buffer.draft : "")
        previousBuffer = buffer
        popup.close()
        Relay.completer.resetTab()
    }

    function send() {
        const t = area.text
        if (t.trim().length === 0)
            return
        // Like WeeChat's paste confirmation: a multi-line paste shouldn't flood a channel by accident.
        if (lines.length > 1 && !confirming) {
            confirming = true
            return
        }
        confirming = false
        if (Relay.sendInput(t)) {
            bar.sent()
            setText("")
            if (buffer) buffer.draft = ""
        }
    }

    function updateSuggestions() {
        if (settingText)
            return
        ctx = Relay.completer.context(area.text, area.cursorPosition)
        const k = ctx.kind
        if (k === "emoji" || k === "command" || k === "mention" || k === "channel") {
            const items = Relay.completer.suggestions(k, ctx.word, 8)
            if (items.length > 0) {
                popup.items = items
                popup.kind = k
                popup.currentIndex = 0
                popup.open()
                return
            }
        }
        popup.close()
    }

    function acceptSuggestion(item) {
        const c = ctx
        let insert = item.insert
        const after = area.text.substring(c.end)
        if (popup.kind !== "emoji" && popup.kind !== "command" && !after.startsWith(" "))
            insert += (popup.kind === "mention" && c.start === 0) ? ": " : " "
        if (popup.kind === "emoji") {
            Emoji.use(item.insert)
            if (!after.startsWith(" ") && after.length > 0) insert += " "
        }
        const t = area.text.substring(0, c.start) + insert + after
        setText(t, c.start + insert.length)
        popup.close()
    }

    Rectangle { anchors.fill: parent; color: Theme.chat }

    Rectangle {
        id: box
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.topMargin: 4
        implicitHeight: Math.max(46, Math.min(scroller.contentHeight, 220) + 16)
        radius: Theme.radiusLarge
        color: Theme.field
        border.width: area.activeFocus ? 1.5 : 1
        border.color: area.activeFocus ? Theme.alpha(Theme.accent, 0.7) : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 6
            spacing: 4

            ScrollView {
                id: scroller
                // Desktop: wheel and scrollbar scroll; a mouse drag selects instead of flicking.
                Binding { target: scroller.contentItem; property: "acceptedButtons"; value: Qt.NoButton; when: scroller.contentItem instanceof Flickable }
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 8
                Layout.bottomMargin: 8
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                TextArea {
                    id: area
                    placeholderText: !bar.buffer ? "" : bar.buffer.kind === "channel" ? qsTr("Message %1").arg(bar.buffer.name)
                                   : bar.buffer.kind === "private" ? qsTr("Message %1").arg(bar.buffer.name)
                                   : qsTr("Type a command for %1").arg(bar.buffer.name)
                    placeholderTextColor: Theme.faint
                    color: Theme.text
                    font.pixelSize: Theme.fontSize
                    wrapMode: TextArea.Wrap
                    selectByMouse: true
                    selectionColor: Theme.alpha(Theme.accent, 0.4)
                    selectedTextColor: Theme.text
                    padding: 0
                    topPadding: 2
                    bottomPadding: 2
                    background: null
                    focus: true
                    persistentSelection: true

                    onTextChanged: {
                        bar.confirming = false
                        if (!bar.settingText)
                            Relay.completer.resetTab()
                        bar.updateSuggestions()
                    }
                    onCursorPositionChanged: if (!bar.settingText && popup.opened) bar.updateSuggestions()

                    Keys.onPressed: (e) => {
                        const shift = e.modifiers & Qt.ShiftModifier
                        const ctrl = e.modifiers & Qt.ControlModifier
                        if (popup.opened) {
                            if (e.key === Qt.Key_Down || (e.key === Qt.Key_N && ctrl)) { popup.move(1); e.accepted = true; return }
                            if (e.key === Qt.Key_Up || (e.key === Qt.Key_P && ctrl)) { popup.move(-1); e.accepted = true; return }
                            if (e.key === Qt.Key_Tab || e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                                bar.acceptSuggestion(popup.items[popup.currentIndex]); e.accepted = true; return
                            }
                            if (e.key === Qt.Key_Escape) { popup.close(); e.accepted = true; return }
                        }
                        if (e.matches(StandardKey.Copy) && area.selectedText.length === 0) {
                            bar.copyFallback(); e.accepted = true; return
                        }
                        // Text fields claim Ctrl+K (delete to end of line) before window shortcuts see it.
                        if (ctrl && e.key === Qt.Key_K) { bar.openSwitcher(); e.accepted = true; return }
                        switch (e.key) {
                        case Qt.Key_Return:
                        case Qt.Key_Enter:
                            if (!shift) { bar.send(); e.accepted = true }
                            break
                        case Qt.Key_Tab:
                        case Qt.Key_Backtab: {
                            const r = Relay.completer.tab(area.text, area.cursorPosition, e.key === Qt.Key_Backtab || shift)
                            if (r.text !== undefined) bar.setText(r.text, r.cursor)
                            e.accepted = true
                            break
                        }
                        case Qt.Key_Up:
                            if (area.text.lastIndexOf("\n", area.cursorPosition - 1) < 0 && !ctrl) {
                                const t = Relay.historyUp(area.text)
                                if (t !== area.text) bar.setText(t)
                                e.accepted = true
                            }
                            break
                        case Qt.Key_Down:
                            if (area.text.indexOf("\n", area.cursorPosition) < 0 && !ctrl) {
                                bar.setText(Relay.historyDown())
                                e.accepted = true
                            }
                            break
                        case Qt.Key_PageUp: bar.pageScroll(-1); e.accepted = true; break
                        case Qt.Key_PageDown: bar.pageScroll(1); e.accepted = true; break
                        case Qt.Key_Home: if (ctrl && area.text.length === 0) { bar.jumpEdge(false); e.accepted = true } break
                        case Qt.Key_End: if (ctrl && area.text.length === 0) { bar.jumpEdge(true); e.accepted = true } break
                        case Qt.Key_Escape:
                            if (bar.confirming) bar.confirming = false
                            else bar.escapePressed()
                            e.accepted = true
                            break
                        }
                    }
                }
            }

            IconButton {
                id: emojiBtn
                iconName: "smile"
                tip: qsTr("Emoji (Ctrl+E)")
                checked: emojiPicker.opened
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 7
                onClicked: emojiPicker.opened ? emojiPicker.close() : emojiPicker.openPicker()
            }
            AbstractButton {
                id: sendBtn
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 7
                implicitWidth: 32
                implicitHeight: 32
                enabled: area.text.trim().length > 0
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                onClicked: bar.send()
                background: Rectangle {
                    radius: Theme.radius
                    color: sendBtn.enabled ? (sendBtn.hovered ? Qt.lighter(Theme.accent, 1.08) : Theme.accent) : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }
                contentItem: Item {
                    Icon {
                        anchors.centerIn: parent
                        name: "send"
                        size: 16
                        color: sendBtn.enabled ? Theme.onAccent : Theme.faint
                    }
                }
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Send (Enter) · Shift+Enter for a new line")
            }
        }
    }

    Text {
        id: hint
        anchors.left: box.left
        anchors.top: box.bottom
        anchors.leftMargin: 14
        anchors.topMargin: 3
        readonly property bool shown: !bar.confirming && text.length > 0
        visible: shown
        text: {
            if (bar.lines.length > 1) return qsTr("Sends %1 separate lines").arg(bar.lines.length)
            if (area.text.startsWith("/") && !area.text.startsWith("//")) return qsTr("Command — runs in WeeChat")
            return ""
        }
        color: Theme.faint
        font.pixelSize: Theme.tiny
    }

    // The multi-line confirmation, in place of the hint.
    RowLayout {
        id: confirmRow
        anchors.left: box.left
        anchors.right: box.right
        anchors.top: box.bottom
        anchors.leftMargin: 10
        anchors.topMargin: 4
        visible: bar.confirming
        spacing: 8
        Icon { name: "info"; size: 15; color: Theme.warn }
        Text {
            Layout.fillWidth: true
            text: qsTr("Send %1 lines as %1 separate messages?").arg(bar.lines.length)
            color: Theme.text
            font.pixelSize: Theme.small
            elide: Text.ElideRight
        }
        AppButton { text: qsTr("Join into one line"); implicitHeight: 28; onClicked: bar.joinLines() }
        AppButton { text: qsTr("Cancel (Esc)"); implicitHeight: 28; onClicked: { bar.confirming = false; area.forceActiveFocus() } }
        AppButton { primary: true; text: qsTr("Send (Enter)"); implicitHeight: 28; onClicked: bar.send() }
    }

    CompletionPopup {
        id: popup
        x: box.x + 8
        y: box.y - height - 6
        width: Math.min(420, box.width - 16)
        onPicked: (item) => bar.acceptSuggestion(item)
    }

    EmojiPicker {
        id: emojiPicker
        x: box.x + box.width - width
        y: box.y - height - 6
        onPicked: (emoji) => {
            area.insert(area.cursorPosition, emoji)
            Emoji.use(emoji)
        }
        onClosed: area.forceActiveFocus()
    }
}
