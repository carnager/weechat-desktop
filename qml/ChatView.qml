import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import WeechatDesktop.Core

Rectangle {
    id: chat
    color: Theme.chat

    signal openSwitcher()
    signal openMentions()
    signal openSettings()
    signal nickClicked(string nick, Item item)

    readonly property var buffer: Relay.current
    readonly property bool inputEmpty: input.text.length === 0
    property var flashId: null
    property var pendingScrollId: null
    property bool stickToBottom: true
    // The messages stay invisible until the opened buffer's history is in and the view is in
    // place, then fade in once: no visible re-layout while lines, wrapping and position settle.
    readonly property bool historyReady: !!buffer && buffer.historyReady
    property bool revealed: false
    onHistoryReadyChanged: if (historyReady && !revealed) Qt.callLater(revealSoon)
    function revealSoon() {
        positionForNewBuffer()
        revealTimer.restart()
    }
    Timer { // one more layout pass before showing (delegate heights settle after the first)
        id: revealTimer
        interval: 32
        onTriggered: {
            if (chat.stickToBottom)
                list.positionViewAtEnd()
            chat.revealed = true
        }
    }
    Timer { // never leave the view blank if the history request fails
        id: revealFallback
        interval: 1500
        onTriggered: chat.revealed = true
    }
    property int newWhileAway: 0

    // ---- selection across messages: anchor (aRow, aPos) to focus (bRow, bPos), in model rows
    // and character positions of each row's text item. Rows read rangeFor(index) to highlight.
    QtObject {
        id: chatSel
        property bool active: false
        property int aRow: -1
        property int aPos: 0
        property int bRow: -1
        property int bPos: 0
        readonly property bool forward: aRow < bRow || (aRow === bRow && aPos <= bPos)
        readonly property int startRow: forward ? aRow : bRow
        readonly property int startPos: forward ? aPos : bPos
        readonly property int endRow: forward ? bRow : aRow
        readonly property int endPos: forward ? bPos : aPos
        readonly property bool empty: !active || (aRow === bRow && aPos === bPos)
        /// [start, end] of the selected characters in [row] (end -1 = to the end), or null.
        function rangeFor(row) {
            if (empty || row < startRow || row > endRow)
                return null
            return [row === startRow ? startPos : 0, row === endRow ? endPos : -1]
        }
        function clear() { active = false; aRow = bRow = -1 }
    }
    readonly property bool hasSelection: !chatSel.empty

    function selectedText() {
        if (chatSel.empty || !buffer)
            return ""
        const r1 = chatSel.startRow, r2 = chatSel.endRow
        if (r1 === r2) {
            const item = list.itemAtIndex(r1)
            return item ? item.textItem.getText(chatSel.startPos, chatSel.endPos) : ""
        }
        // Several lines: IRC-style transcript ("[time] <nick> " head from the model, the text as
        // shown), the first and last cut where the selection is.
        const out = []
        for (let r = r1; r <= r2; ++r) {
            const item = list.itemAtIndex(r)
            const full = buffer.lines.textOfRows(r, r)
            if (!item) {
                out.push(full)
                continue
            }
            const t = item.textItem
            const head = full.slice(0, full.length - item.plain.length)
            const body = r === r1 ? t.getText(chatSel.startPos, t.length)
                       : r === r2 ? t.getText(0, chatSel.endPos)
                       : t.getText(0, t.length)
            if ((r === r1 || r === r2) && body.length === 0)
                continue // the selection only touches this line's edge
            out.push(full.endsWith(item.plain) ? head + body : body)
        }
        return out.join("\n")
    }
    function copySelection() {
        const t = selectedText()
        if (t.length) {
            Platform.copy(t)
            return true
        }
        return false
    }

    function insertText(t) { input.insert(t); input.focusInput() }
    function sendInput() { input.send() } // as if Enter was pressed
    function openEmojiPicker() { input.openEmoji() }
    function openSearch() { search.open() }

    function scrollToLine(id) {
        pendingScrollId = id
        tryPendingScroll()
    }
    function tryPendingScroll() {
        if (pendingScrollId === null || !buffer)
            return
        const row = buffer.lines.rowForId(pendingScrollId)
        if (row < 0)
            return
        stickToBottom = false
        list.positionViewAtIndex(row, ListView.Center)
        flash(pendingScrollId)
        pendingScrollId = null
    }
    function flash(id) {
        flashId = id
        flashTimer.restart()
    }
    Timer { id: flashTimer; interval: 1600; onTriggered: chat.flashId = null }

    function handleLink(link) {
        if (link.startsWith("channel:"))
            Relay.joinChannel(link.substring(8))
        else
            Qt.openUrlExternally(link)
    }

    /// Scroll by [dy] pixels (positive = towards newer lines), clamped to the content.
    function scrollBy(dy) {
        if (list.count === 0)
            return
        const top = list.originY
        const bottom = list.originY + list.contentHeight - list.height
        if (bottom <= top)
            return
        if (dy < 0)
            stickToBottom = false
        list.contentY = Math.max(top, Math.min(bottom, list.contentY + dy))
        if (dy > 0 && list.contentY >= bottom - 1)
            scrollToBottom()
    }

    function scrollToBottom() {
        stickToBottom = true
        newWhileAway = 0
        list.positionViewAtEnd()
    }

    // When the buffer changes: open at the unread marker if there is one, else at the bottom.
    onBufferChanged: {
        chatSel.clear()
        revealed = false
        revealFallback.restart()
        if (historyReady)
            Qt.callLater(revealSoon)
        newWhileAway = 0
        search.close()
        Qt.callLater(positionForNewBuffer)
    }
    function positionForNewBuffer() {
        if (!buffer)
            return
        const marker = buffer.lines.rowOfUnreadMarker()
        if (marker >= 0 && marker < list.count - 3) {
            stickToBottom = false
            list.positionViewAtIndex(marker, ListView.Beginning)
            list.contentY -= 30
        } else {
            scrollToBottom()
        }
        input.focusInput()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ChatHeader {
            Layout.fillWidth: true
            visible: !!chat.buffer
            onOpenSearch: search.open()
            onLinkActivated: (link) => chat.handleLink(link)
        }

        StatusBanner {
            Layout.fillWidth: true
        }

        // ---- search bar
        Rectangle {
            id: search
            property int matchRow: -1
            property int matches: 0
            function open() {
                visible = true
                field.forceActiveFocus()
                field.selectAll()
            }
            function close() {
                visible = false
                matchRow = -1
                field.text = ""
                input.focusInput()
            }
            function step(older) {
                if (!chat.buffer || field.text.length === 0)
                    return
                const lines = chat.buffer.lines
                let r = older ? lines.findPrevious(field.text, matchRow < 0 ? lines.count : matchRow)
                              : lines.findNext(field.text, matchRow)
                if (r < 0 && older && chat.buffer.hasMoreHistory) {
                    Relay.loadMoreHistory()
                    return
                }
                if (r >= 0) {
                    matchRow = r
                    chat.stickToBottom = false
                    list.positionViewAtIndex(r, ListView.Center)
                    chat.flash(list.itemAtIndex(r) ? list.itemAtIndex(r).lineId : null)
                }
            }

            visible: false
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 48 : 0
            color: Theme.chat
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 12
                spacing: 8
                AppTextField {
                    id: field
                    Layout.fillWidth: true
                    leadingIcon: "search"
                    placeholderText: qsTr("Search this buffer")
                    implicitHeight: 34
                    onTextChanged: {
                        search.matchRow = -1
                        search.matches = chat.buffer ? chat.buffer.lines.countMatches(text) : 0
                    }
                    Keys.onReturnPressed: (e) => search.step(!(e.modifiers & Qt.ShiftModifier))
                    Keys.onEnterPressed: (e) => search.step(!(e.modifiers & Qt.ShiftModifier))
                    Keys.onEscapePressed: search.close()
                    Keys.onUpPressed: search.step(true)
                    Keys.onDownPressed: search.step(false)
                }
                Text {
                    text: field.text.length ? qsTr("%n matches", "", search.matches) : ""
                    color: Theme.muted
                    font.pixelSize: Theme.tiny
                }
                IconButton { iconName: "chevron-up"; tip: qsTr("Older (Enter)"); onClicked: search.step(true) }
                IconButton { iconName: "chevron-down"; tip: qsTr("Newer (Shift+Enter)"); onClicked: search.step(false) }
                IconButton { iconName: "x"; tip: qsTr("Close (Esc)"); onClicked: search.close() }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
        }

        // ---- messages
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                acceptedButtons: Qt.NoButton // desktop: no mouse-drag flicking (touch still works)
                id: list
                anchors.fill: parent
                opacity: chat.revealed ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                model: chat.buffer ? chat.buffer.lines : null
                clip: true
                cacheBuffer: 1600
                boundsBehavior: Flickable.StopAtBounds
                // No bottomMargin: positionViewAtEnd() ignores it, leaving atYEnd false. The footer pads.
                footer: Item { width: 1; height: 10 }
                flickDeceleration: 4000
                maximumFlickVelocity: 6000
                pixelAligned: true

                ScrollBar.vertical: AppScrollBar {}

                delegate: MessageDelegate {
                    selection: chatSel
                    flash: chat.flashId !== null && lineId === chat.flashId
                    onNickClicked: (nick, item) => chat.nickClicked(nick, item)
                    onReply: (nick) => chat.insertText(nick + ": ")
                    onQuote: (nick, text) => chat.insertText("<" + nick + "> " + text + "\n")
                    onLinkActivated: (link) => chat.handleLink(link)
                    onTyped: (text) => chat.insertText(text)
                }

                header: Item {
                    width: list.width
                    height: chat.buffer ? 56 : 0
                    Text {
                        anchors.centerIn: parent
                        visible: !!chat.buffer
                        text: !chat.buffer ? "" : chat.buffer.loadingHistory ? qsTr("Loading history…")
                            : chat.buffer.hasMoreHistory ? qsTr("Scroll up for older messages")
                            : qsTr("This is the beginning of what WeeChat has in memory for %1").arg(chat.buffer.name)
                        color: Theme.faint
                        font.pixelSize: Theme.tiny
                    }
                }

                onMovementStarted: chat.stickToBottom = false
                onMovementEnded: chat.stickToBottom = atYEnd
                onAtYEndChanged: if (atYEnd) { chat.stickToBottom = true; chat.newWhileAway = 0 }
                onCountChanged: {
                    if (chat.stickToBottom)
                        Qt.callLater(list.positionViewAtEnd)
                    chat.tryPendingScroll()
                }
                onContentHeightChanged: if (chat.stickToBottom && !moving) Qt.callLater(list.positionViewAtEnd)
                onHeightChanged: if (chat.stickToBottom) Qt.callLater(list.positionViewAtEnd)
                onContentYChanged: {
                    if (chat.buffer && chat.buffer.hasMoreHistory && !chat.buffer.loadingHistory
                        && count > 0 && !chat.stickToBottom && contentY - originY < 600)
                        Relay.loadMoreHistory()
                }
                Connections {
                    target: chat.buffer
                    // History for a freshly opened buffer arrived and the marker was placed: go there,
                    // unless the user already started scrolling.
                    function onUnreadMarkerPlaced() {
                        if (chat.stickToBottom) Qt.callLater(chat.positionForNewBuffer)
                    }
                }
                Connections {
                    target: chat.buffer ? chat.buffer.lines : null
                    function onRowsInserted(parent, first, last) {
                        if (!chat.stickToBottom && first > 0)
                            chat.newWhileAway += last - first + 1
                    }
                    function onModelReset() {
                        if (chat.stickToBottom) Qt.callLater(chat.positionForNewBuffer)
                    }
                }

                // Wheel and touchpad scrolling. A WheelHandler accepts the event, so the ListView
                // never sees it: this handler has to do the scrolling itself.
                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    target: null
                    onWheel: (e) => {
                        const dy = e.pixelDelta.y !== 0 ? e.pixelDelta.y
                                                        : e.angleDelta.y / 120 * Theme.fontSize * 4
                        if (dy === 0)
                            return
                        chat.scrollBy(-dy)
                    }
                }
            }

            // Mouse chatSel across messages. Presses that aren't on message text (buttons, nicks,
            // avatars, previews, the hover actions) are passed on to whatever is underneath.
            MouseArea {
                id: selector
                anchors.fill: list
                anchors.rightMargin: 14 // leave the scrollbar alone
                acceptedButtons: Qt.LeftButton
                preventStealing: true
                property bool dragging: false
                property point pressPoint
                property var pressItem: null
                property real lastX: 0
                property real lastY: 0

                // {item, pos, inText} for a point in this MouseArea, or null over no row.
                function hit(x, y, clampToView) {
                    if (clampToView)
                        y = Math.max(1, Math.min(height - 1, y))
                    const c = mapToItem(list.contentItem, x, y)
                    const item = list.itemAt(c.x, c.y)
                    if (!item || !item.textItem)
                        return null
                    const t = item.textItem
                    const tp = mapToItem(t, x, y)
                    return {
                        item: item,
                        pos: t.positionAt(tp.x, tp.y),
                        inText: tp.x >= 0 && tp.y >= 0 && tp.x <= t.width && tp.y <= t.height,
                        link: t.linkAt(tp.x, tp.y)
                    }
                }
                function extendTo(x, y) {
                    const h = hit(x, y, true)
                    if (!h)
                        return
                    chatSel.bRow = h.item.index
                    chatSel.bPos = h.pos
                }

                onPressed: (mouse) => {
                    // Anywhere on a row starts a selection (positionAt snaps to the nearest
                    // character), except on things that are clickable themselves.
                    const h = hit(mouse.x, mouse.y, false)
                    if (!h) {
                        mouse.accepted = false
                        return
                    }
                    const local = mapToItem(h.item, mouse.x, mouse.y)
                    if (h.item.interactiveAt(local.x, local.y)) {
                        mouse.accepted = false
                        return
                    }
                    if ((mouse.modifiers & Qt.ShiftModifier) && chatSel.active) {
                        chatSel.bRow = h.item.index
                        chatSel.bPos = h.pos
                        dragging = true
                    } else {
                        chatSel.clear()
                        chatSel.aRow = chatSel.bRow = h.item.index
                        chatSel.aPos = chatSel.bPos = h.pos
                        dragging = false
                    }
                    pressPoint = Qt.point(mouse.x, mouse.y)
                    pressItem = h
                }
                onPositionChanged: (mouse) => {
                    lastX = mouse.x
                    lastY = mouse.y
                    if (!dragging && Math.abs(mouse.x - pressPoint.x) + Math.abs(mouse.y - pressPoint.y) < 4)
                        return
                    dragging = true
                    chatSel.active = true
                    chat.stickToBottom = false
                    extendTo(mouse.x, mouse.y)
                }
                onReleased: (mouse) => {
                    if (!dragging && pressItem && pressItem.inText && pressItem.link)
                        chat.handleLink(pressItem.link)
                    else if (!chatSel.empty)
                        Platform.copySelection(chat.selectedText()) // X11/Wayland primary chatSel
                    dragging = false
                    pressItem = null
                }
                onDoubleClicked: (mouse) => {
                    const h = hit(mouse.x, mouse.y, false)
                    if (!h || !h.inText)
                        return
                    const t = h.item.textItem
                    t.cursorPosition = h.pos
                    t.selectWord()
                    chatSel.active = true
                    chatSel.aRow = chatSel.bRow = h.item.index
                    chatSel.aPos = t.selectionStart
                    chatSel.bPos = t.selectionEnd
                    Platform.copySelection(chat.selectedText())
                }

                // Dragging past the top or bottom edge scrolls, faster the further out.
                Timer {
                    interval: 16
                    repeat: true
                    running: selector.pressed && selector.dragging
                             && (selector.lastY < 0 || selector.lastY > selector.height)
                    onTriggered: {
                        const over = selector.lastY < 0 ? selector.lastY : selector.lastY - selector.height
                        chat.scrollBy(Math.max(-60, Math.min(60, over / 2)))
                        selector.extendTo(selector.lastX, selector.lastY)
                    }
                }
            }

            Shortcut {
                sequences: [StandardKey.Copy]
                enabled: chat.hasSelection
                onActivated: chat.copySelection()
            }

            // Empty state (not during a normal startup: the buffer is usually there within moments)
            Timer { id: startupGrace; interval: 1200; running: true }
            Column {
                anchors.centerIn: parent
                visible: !chat.buffer && (Relay.synced || !startupGrace.running)
                spacing: 12
                Image {
                    anchors.horizontalCenter: parent.horizontalCenter
                    source: "qrc:/resources/icon.svg"
                    sourceSize: Qt.size(128, 128)
                    width: 72; height: 72
                    opacity: 0.85
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: Relay.synced ? qsTr("Pick a buffer to start chatting") : Relay.stateText
                    color: Theme.muted
                    font.pixelSize: Theme.large
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Ctrl+K to jump anywhere · F1 for shortcuts")
                    color: Theme.faint
                    font.pixelSize: Theme.small
                }
            }

            // Jump to bottom
            AbstractButton {
                id: jumpDown
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 18
                visible: opacity > 0
                opacity: !list.atYEnd && list.count > 0 ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.anim } }
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                implicitHeight: 36
                implicitWidth: jumpRow.implicitWidth + 24
                onClicked: chat.scrollToBottom()
                background: Rectangle {
                    radius: height / 2
                    color: chat.newWhileAway > 0 ? Theme.accent : Theme.surface
                    border.width: chat.newWhileAway > 0 ? 0 : 1
                    border.color: Theme.border
                }
                contentItem: Row {
                    id: jumpRow
                    spacing: 6
                    anchors.centerIn: parent
                    Icon {
                        name: "arrow-down"
                        size: 16
                        color: chat.newWhileAway > 0 ? Theme.onAccent : Theme.text
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        visible: chat.newWhileAway > 0
                        text: qsTr("%n new", "", chat.newWhileAway)
                        color: Theme.onAccent
                        font.pixelSize: Theme.small
                        font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }

        InputBar {
            id: input
            Layout.fillWidth: true
            visible: !!chat.buffer
            buffer: chat.buffer
            onPageScroll: (dir) => {
                chat.stickToBottom = false
                list.contentY = Math.max(list.originY,
                                         Math.min(list.contentY + dir * list.height * 0.85,
                                                  list.contentHeight + list.originY - list.height))
                if (list.atYEnd) chat.scrollToBottom()
            }
            onJumpEdge: (bottom) => bottom ? chat.scrollToBottom() : list.positionViewAtBeginning()
            onOpenSwitcher: chat.openSwitcher()
            onCopyFallback: chat.copySelection()
            onSent: chat.scrollToBottom()
            onEscapePressed: {
                if (chat.hasSelection) chatSel.clear()
                else if (search.visible) search.close()
                else chat.scrollToBottom()
            }
        }
    }
}
