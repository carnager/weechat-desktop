import QtQuick
import WeechatDesktop.Core

// The menu for a nick (click in chat or the nicklist): a native desktop menu.
QtObject {
    id: menu
    property string nick
    signal mention(string nick)
    signal info(string nick)

    // The menu icons are the app's own (Icon.qml): op and voice show IRC's @ and +.
    property Icon glyphs: Icon { visible: false }
    function withIcons(entries) {
        return entries.map(e => e.icon ? Object.assign({ svg: glyphs.paths[e.icon] || "" }, e) : e)
    }

    readonly property bool isChannel: !!Relay.current && Relay.current.kind === "channel"

    function popupFor(n, item) {
        nick = n
        const entries = [
            { section: n },
            { id: "query", text: qsTr("Send message"), icon: "message" },
            { id: "mention", text: qsTr("Mention"), icon: "reply" },
            { id: "info", text: qsTr("Info…"), icon: "info" },
            { id: "copy", text: qsTr("Copy nick"), icon: "copy" }
        ]
        if (isChannel) {
            entries.push({ separator: true },
                { id: "op", text: qsTr("Give op"), icon: "at" },
                { id: "voice", text: qsTr("Give voice"), icon: "plus" },
                { id: "quiet", text: qsTr("Quiet"), icon: "mic-off" },
                { separator: true },
                { id: "kick", text: qsTr("Kick"), icon: "user-minus" },
                { id: "kickban", text: qsTr("Kick and ban"), icon: "ban" })
        }
        if (item)
            Platform.popupMenu("nick", withIcons(entries), item, 0, item.height + 2)
        else
            Platform.popupMenu("nick", withIcons(entries), null, 0, 0)
    }

    property Connections conn: Connections {
        target: Platform
        function onMenuTriggered(tag, id) {
            if (tag !== "nick")
                return
            const n = menu.nick
            switch (id) {
            case "query": Relay.openQuery(n); break
            case "mention": menu.mention(n); break
            case "info": menu.info(n); break
            case "copy": Platform.copy(n); break
            case "op": Relay.command("/op " + n); break
            case "voice": Relay.command("/voice " + n); break
            case "quiet": Relay.command("/quiet " + n); break
            case "kick": Relay.command("/kick " + n); break
            case "kickban": Relay.command("/kickban " + n); break
            }
        }
    }
}
