import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import WeechatDesktop.Core

// One line of a buffer. "cozy" shows avatar + nick headers over grouped messages; "compact" is
// IRC-style "time nick text" rows. Join/part and other events render as quiet one-liners.
Item {
    id: msg
    required property int index
    required property var lineId
    required property string time
    required property string fullTime
    required property string nick
    required property color nickColor
    required property string prefix
    required property string html
    required property string plain
    required property string kind
    required property bool highlight
    required property bool self
    required property bool groupStart
    required property bool dayBreak
    required property string dayLabel
    required property var urls
    required property bool emojiOnly
    required property bool unreadMarker

    property bool flash: false
    property bool compact: AppSettings.layout === "compact"

    // ---- selection across messages (driven by ChatView, which owns the mouse for it)
    /// The ChatView's selection object; null outside a chat view.
    property QtObject selection: null
    /// The text item that shows this line, for hit testing and highlighting.
    readonly property Item textItem: isEvent ? eventText : compact ? compactText : cozyText
    /// The hover action box: presses there go to its buttons, not to text selection.
    readonly property Item actionsBox: actionsRect
    readonly property var selRange: selection ? selection.rangeFor(index) : null
    onSelRangeChanged: applySelection()
    onTextItemChanged: applySelection()
    ListView.onReused: applySelection()
    Component.onCompleted: applySelection()
    /// True if ([x], [y]) in this item is on something clickable (nick, avatar, preview, hover
    /// actions), which a press there should reach instead of starting a text selection.
    function interactiveAt(x, y) {
        const items = [actionsRect, compactNick, headerNick, avatar]
        for (let i = 0; i < previews.count; ++i)
            items.push(previews.itemAt(i))
        for (const it of items) {
            if (!it || !it.visible)
                continue
            const p = mapToItem(it, x, y)
            if (p.x >= 0 && p.y >= 0 && p.x <= it.width && p.y <= it.height)
                return true
        }
        return false
    }
    function applySelection() {
        const t = textItem
        if (!t)
            return
        for (const other of [eventText, compactText, cozyText])
            if (other !== t) other.deselect()
        if (!selRange)
            t.deselect()
        else
            t.select(Math.max(0, selRange[0]), selRange[1] < 0 ? t.length : Math.min(selRange[1], t.length))
    }

    signal nickClicked(string nick, Item item)
    signal reply(string nick)
    signal quote(string nick, string text)
    signal linkActivated(string link)
    /// Printable keys typed while a message has focus belong in the input.
    signal typed(string text)

    readonly property bool isChat: kind === "message" || kind === "notice" || kind === "action"
    readonly property bool isEvent: !isChat
    readonly property bool showHeader: isChat && groupStart && !compact && kind !== "action"
    readonly property real gutter: compact ? 0 : 64

    width: ListView.view ? ListView.view.width : 400
    implicitHeight: column.implicitHeight

    Column {
        id: column
        width: parent.width

        // Day separator
        Item {
            visible: msg.dayBreak
            width: parent.width
            height: visible ? 40 : 0
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                height: 1
                color: Theme.border
            }
            Rectangle {
                anchors.centerIn: parent
                width: dayText.implicitWidth + 20
                height: 22
                radius: 11
                color: Theme.chat
                border.width: 1
                border.color: Theme.border
                Text {
                    id: dayText
                    anchors.centerIn: parent
                    text: msg.dayLabel
                    color: Theme.muted
                    font.pixelSize: Theme.tiny
                    font.weight: Font.DemiBold
                }
            }
        }

        // "New messages" divider
        Item {
            visible: msg.unreadMarker
            width: parent.width
            height: visible ? 24 : 0
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: newLabel.left
                anchors.leftMargin: 20
                anchors.rightMargin: 8
                height: 1
                color: Theme.unreadMarker
            }
            Text {
                id: newLabel
                anchors.right: parent.right
                anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("NEW")
                color: Theme.unreadMarker
                font.pixelSize: Theme.tiny
                font.weight: Font.Bold
                font.letterSpacing: 0.8
            }
        }

        Item { // spacing above a new group
            width: 1
            height: msg.showHeader && !msg.dayBreak && !msg.unreadMarker ? 10 : 0
        }

        // The message row itself
        Item {
            id: body
            width: parent.width
            height: content.implicitHeight + (msg.isEvent ? 2 : (msg.compact ? 2 : 4))

            HoverHandler { id: hover }

            Rectangle { // hover / mention / flash background
                anchors.fill: parent
                color: msg.flash ? Theme.alpha(Theme.warn, 0.18)
                     : msg.highlight ? Theme.mentionBg
                     : (hover.hovered && msg.isChat ? Theme.hover : "transparent")
                Behavior on color { ColorAnimation { duration: 400 } }
                Rectangle {
                    visible: msg.highlight
                    width: 3
                    height: parent.height
                    color: Theme.accent
                }
            }

            Item {
                id: content
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: msg.compact ? 16 : 20
                anchors.rightMargin: 20
                implicitHeight: msg.isEvent ? eventRow.implicitHeight + 4
                              : msg.compact ? compactRow.implicitHeight + 2
                              : cozy.implicitHeight + 2

                // ---- events (join/part/…): a quiet one-liner. Anchors for the same reason as below.
                Item {
                    id: eventRow
                    visible: msg.isEvent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: msg.compact ? 0 : msg.gutter - 20
                    implicitHeight: Math.max(eventText.implicitHeight, eventIcon.implicitHeight)
                    Text {
                        id: eventTime
                        visible: msg.compact
                        width: visible ? 44 : 0
                        text: msg.time
                        color: Theme.faint
                        font.pixelSize: Theme.tiny
                        font.features: { "tnum": 1 }
                        topPadding: 2
                    }
                    Text {
                        id: eventIcon
                        x: eventTime.visible ? eventTime.width + 8 : 0
                        text: msg.kind === "join" ? "→" : (msg.kind === "part" || msg.kind === "quit") ? "←"
                            : msg.kind === "nick" ? "✎" : msg.kind === "topic" ? "✦" : msg.kind === "mode" ? "⚑" : "•"
                        color: msg.kind === "join" ? Theme.ok : (msg.kind === "part" || msg.kind === "quit") ? Theme.danger : Theme.faint
                        font.pixelSize: Theme.small
                    }
                    TextEdit {
                        id: eventText
                        anchors.left: eventIcon.right
                        anchors.leftMargin: 8
                        anchors.right: eventHoverTime.visible ? eventHoverTime.left : parent.right
                        anchors.rightMargin: eventHoverTime.visible ? 8 : 0
                        readOnly: true
                        selectByMouse: false
                        persistentSelection: true
                        textFormat: TextEdit.RichText
                        wrapMode: TextEdit.Wrap
                        text: msg.isEvent ? msg.html : ""
                        color: Theme.muted
                        font.pixelSize: Theme.small
                        selectionColor: Theme.alpha(Theme.accent, 0.35)
                        onLinkActivated: (link) => msg.linkActivated(link)
                    }
                    Text {
                        id: eventHoverTime
                        visible: !msg.compact && hover.hovered
                        anchors.right: parent.right
                        text: msg.time
                        color: Theme.faint
                        font.pixelSize: Theme.tiny
                    }
                }

                // ---- compact chat row: "time  nick  text"
                // Plain anchors, not a RowLayout: layouts size their children in a later polish, so
                // a new row would first report an unwrapped height and then grow. The ListView,
                // pinned to the bottom, reacts by repositioning and creating more rows, forever.
                Item {
                    id: compactRow
                    visible: msg.isChat && msg.compact
                    anchors.left: parent.left
                    anchors.right: parent.right
                    implicitHeight: Math.max(compactText.implicitHeight, compactNick.implicitHeight)
                    Text {
                        id: compactTime
                        width: 44
                        text: msg.time
                        color: Theme.faint
                        font.pixelSize: Theme.tiny
                        font.features: { "tnum": 1 }
                        topPadding: 2
                    }
                    Text {
                        id: compactNick
                        x: compactTime.width + 10
                        width: 120
                        text: msg.kind === "action" ? "*" : msg.nick
                        color: msg.kind === "action" ? Theme.muted : msg.nickColor
                        font.pixelSize: Theme.fontSize
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideLeft
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: msg.nickClicked(msg.nick, compactNick)
                        }
                    }
                    MessageText {
                        id: compactText
                        anchors.left: compactNick.right
                        anchors.leftMargin: 10
                        anchors.right: parent.right
                        delegateRoot: msg
                    }
                }

                // ---- cozy: avatar + header on group start, aligned text after
                Item {
                    id: cozy
                    visible: msg.isChat && !msg.compact
                    anchors.left: parent.left
                    anchors.right: parent.right
                    implicitHeight: cozyCol.implicitHeight

                    Avatar {
                        id: avatar
                        visible: msg.showHeader
                        nick: msg.nick
                        tint: msg.nickColor
                        size: 36
                        y: 2
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: msg.nickClicked(msg.nick, parent)
                        }
                    }
                    Text { // hover timestamp in the gutter of continuation lines
                        visible: !msg.showHeader && hover.hovered
                        text: msg.time
                        color: Theme.faint
                        font.pixelSize: Theme.tiny
                        font.features: { "tnum": 1 }
                        width: msg.gutter - 28
                        horizontalAlignment: Text.AlignRight
                        y: 4
                    }

                    Column {
                        id: cozyCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: msg.gutter - 20
                        spacing: 2

                        Row {
                            visible: msg.showHeader
                            spacing: 8
                            Text {
                                id: headerNick
                                text: msg.nick
                                color: msg.nickColor
                                font.pixelSize: Theme.fontSize
                                font.weight: Font.Bold
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: msg.nickClicked(msg.nick, headerNick)
                                }
                            }
                            Rectangle {
                                visible: msg.kind === "notice"
                                width: noticeText.implicitWidth + 10
                                height: 16
                                radius: 4
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.alpha(Theme.warn, 0.18)
                                Text {
                                    id: noticeText
                                    anchors.centerIn: parent
                                    text: qsTr("notice")
                                    color: Theme.warn
                                    font.pixelSize: Theme.tiny
                                    font.weight: Font.DemiBold
                                }
                            }
                            Text {
                                text: msg.time
                                color: Theme.faint
                                font.pixelSize: Theme.tiny
                                anchors.baseline: headerNick.baseline
                                ToolTip.visible: timeHover.hovered
                                ToolTip.delay: 500
                                ToolTip.text: msg.fullTime
                                HoverHandler { id: timeHover }
                            }
                        }

                        MessageText {
                            id: cozyText
                            width: parent.width
                            delegateRoot: msg
                        }

                        // Link previews / inline images
                        Repeater {
                            id: previews
                            model: AppSettings.linkPreviews || AppSettings.inlineImages ? msg.urls.slice(0, 3) : []
                            delegate: LinkPreview {
                                required property string modelData
                                url: modelData
                                maxWidth: Math.min(cozyCol.width, 520)
                            }
                        }
                    }
                }
            }

            // Hover actions
            Rectangle {
                id: actionsRect
                visible: hover.hovered && msg.isChat
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.top: parent.top
                anchors.topMargin: -14
                z: 5
                width: actions.implicitWidth + 6
                height: 32
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.border
                Row {
                    id: actions
                    anchors.centerIn: parent
                    spacing: 0
                    IconButton { iconName: "reply"; iconSize: 15; implicitWidth: 28; implicitHeight: 28; tip: qsTr("Reply"); onClicked: msg.reply(msg.nick) }
                    IconButton { iconName: "message"; iconSize: 15; implicitWidth: 28; implicitHeight: 28; tip: qsTr("Quote"); onClicked: msg.quote(msg.nick, msg.plain) }
                    IconButton { iconName: "copy"; iconSize: 15; implicitWidth: 28; implicitHeight: 28; tip: qsTr("Copy text"); onClicked: Platform.copy(msg.plain) }
                    IconButton {
                        id: moreBtn
                        iconName: "user"; iconSize: 15; implicitWidth: 28; implicitHeight: 28; tip: qsTr("Nick actions")
                        onClicked: msg.nickClicked(msg.nick, moreBtn)
                    }
                }
            }
        }
    }

    // Message body text, shared by both layouts.
    component MessageText: TextEdit {
        id: textItem
        required property Item delegateRoot
        readOnly: true
        // ChatView does the mouse selection, so it can span messages.
        selectByMouse: false
        persistentSelection: true
        textFormat: TextEdit.RichText
        wrapMode: TextEdit.Wrap
        text: delegateRoot.kind === "action"
              ? "<i><span style=\"color:" + delegateRoot.nickColor + ";font-weight:600\">" + Platform.escape(delegateRoot.nick) + "</span> " + delegateRoot.html + "</i>"
              : delegateRoot.html
        color: delegateRoot.kind === "notice" ? Theme.muted : Theme.text
        font.pixelSize: delegateRoot.emojiOnly ? Theme.fontSize * 2.4 : Theme.fontSize
        selectionColor: Theme.alpha(Theme.accent, 0.35)
        selectedTextColor: Theme.text
        onLinkActivated: (link) => delegateRoot.linkActivated(link)
        Keys.onPressed: (e) => {
            if (e.text.length > 0 && e.text.charCodeAt(0) >= 32 && !(e.modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
                delegateRoot.typed(e.text)
                e.accepted = true
            }
        }
        // The pointer cursor and the link tooltip come from ChatView's selector, which is on top.
    }
}
