import QtQuick
import WeechatDesktop.Core

// The menu for a nick (click in chat or the nicklist): a native desktop menu.
QtObject {
    id: menu
    property string nick
    signal mention(string nick)
    signal info(string nick)

    readonly property bool isChannel: !!Relay.current && Relay.current.kind === "channel"

    function popupFor(n, item) {
        nick = n
        const entries = [
            { section: n },
            { id: "query", text: qsTr("Send message"), icon: "mail-message-new" },
            { id: "mention", text: qsTr("Mention"), icon: "insert-text" },
            { id: "info", text: qsTr("Info…"), icon: "dialog-information" },
            { id: "copy", text: qsTr("Copy nick"), icon: "edit-copy" }
        ]
        if (isChannel) {
            entries.push({ separator: true },
                { id: "op", text: qsTr("Give op"), icon: "emblem-favorite" },
                { id: "voice", text: qsTr("Give voice"), icon: "audio-volume-high" },
                { id: "quiet", text: qsTr("Quiet"), icon: "audio-volume-muted" },
                { separator: true },
                { id: "kick", text: qsTr("Kick"), icon: "im-kick-user" },
                { id: "kickban", text: qsTr("Kick and ban"), icon: "im-ban-user" })
        }
        if (item)
            Platform.popupMenu("nick", entries, item, 0, item.height + 2)
        else
            Platform.popupMenu("nick", entries, null, 0, 0)
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
