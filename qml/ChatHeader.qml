import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import WeechatDesktop.Core

Rectangle {
    id: header
    property var buffer: Relay.current
    property bool topicExpanded: false
    signal openSearch()
    signal openLinks()
    signal openMentions()
    signal openSwitcher()
    signal linkActivated(string link)

    color: Theme.chat
    implicitHeight: Math.max(54, layout.implicitHeight + 16)

    onBufferChanged: topicExpanded = false

    RowLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 14
        anchors.rightMargin: 10
        spacing: 10

        IconButton {
            visible: !AppSettings.showSidebar || header.width < 700
            iconName: "panel-left"
            tip: qsTr("Toggle sidebar (Ctrl+B)")
            onClicked: AppSettings.showSidebar = !AppSettings.showSidebar
        }

        Icon {
            visible: !!header.buffer
            name: !header.buffer ? "" : header.buffer.kind === "channel" ? "hash"
                : header.buffer.kind === "private" ? "user" : header.buffer.kind === "server" ? "server" : "terminal"
            size: 20
            color: Theme.muted
            Layout.alignment: header.topicExpanded ? Qt.AlignTop : Qt.AlignVCenter
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            RowLayout {
                spacing: 8
                Text {
                    text: header.buffer ? (header.buffer.kind === "channel" ? header.buffer.name.replace(/^#/, "") : header.buffer.name) : ""
                    color: Theme.text
                    font.pixelSize: Theme.large
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                    Layout.maximumWidth: header.width * 0.5
                }
                Text {
                    visible: !!header.buffer && header.buffer.server.length > 0 && header.buffer.kind !== "server"
                    text: header.buffer ? header.buffer.server : ""
                    color: Theme.faint
                    font.pixelSize: Theme.tiny
                }
                Text {
                    visible: !!header.buffer && header.buffer.kind === "channel" && header.buffer.nicks.count > 0
                    text: header.buffer ? "· " + qsTr("%n members", "", header.buffer.nicks.count) : ""
                    color: Theme.faint
                    font.pixelSize: Theme.tiny
                }
                Text {
                    visible: !!header.buffer && header.buffer.kind === "channel" && !header.buffer.joined
                    text: "· " + qsTr("not joined")
                    color: Theme.warn
                    font.pixelSize: Theme.tiny
                }
            }
            TextEdit {
                id: topic
                Layout.fillWidth: true
                visible: !!header.buffer && header.buffer.title.length > 0
                readOnly: true
                selectByMouse: true
                textFormat: TextEdit.RichText
                wrapMode: header.topicExpanded ? TextEdit.Wrap : TextEdit.NoWrap
                text: header.buffer ? header.buffer.titleHtml : ""
                color: Theme.muted
                font.pixelSize: Theme.small
                clip: true
                Layout.maximumHeight: header.topicExpanded ? 200 : font.pixelSize * 1.5
                onLinkActivated: (link) => header.linkActivated(link)
                HoverHandler { cursorShape: topic.hoveredLink ? Qt.PointingHandCursor : Qt.PointingHandCursor }
                TapHandler {
                    onTapped: if (!topic.hoveredLink) header.topicExpanded = !header.topicExpanded
                }
                ToolTip.visible: topicHover.hovered && !header.topicExpanded && !topic.hoveredLink
                ToolTip.delay: 900
                ToolTip.text: qsTr("Click to expand the topic")
                HoverHandler { id: topicHover }
            }
        }

        IconButton {
            iconName: "external"
            tip: qsTr("Links in this buffer (Ctrl+Shift+O)")
            onClicked: header.openLinks()
        }
        IconButton {
            iconName: "search"
            tip: qsTr("Search in buffer (Ctrl+F)")
            onClicked: header.openSearch()
        }
        IconButton {
            visible: !!header.buffer && header.buffer.kind === "channel"
            iconName: "users"
            checked: AppSettings.showNicklist
            tip: qsTr("Members (Ctrl+Shift+N)")
            onClicked: AppSettings.showNicklist = !AppSettings.showNicklist
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }
}
