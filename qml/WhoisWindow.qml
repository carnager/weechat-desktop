import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import WeechatDesktop.Core

// Who someone is: a separate window filled from /whois (see Relay.requestWhois).
Window {
    id: win
    flags: Qt.Dialog
    title: qsTr("%1 — Info").arg(nick)
    width: 440
    height: Math.min(560, Math.max(260, content.implicitHeight + 32))
    minimumWidth: 340
    minimumHeight: 220
    color: Theme.window

    readonly property var info: Relay.whois
    readonly property string nick: info.nick || ""
    readonly property color tint: Platform.nickColor(nick, Theme.dark)

    signal mention(string nick)

    function showFor(n) {
        Relay.requestWhois(n)
        show()
        raise()
        requestActivate()
    }

    Shortcut { sequences: [StandardKey.Close, "Escape"]; onActivated: win.close() }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            id: content
            width: parent.width
            spacing: 14

            Item { Layout.preferredHeight: 4 }

            RowLayout {
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                spacing: 14
                Avatar { nick: win.nick; tint: win.tint; size: 52 }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: win.nick
                        color: win.tint
                        font.pixelSize: Theme.large + 4
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: win.info.realName || ""
                        color: Theme.text
                        font.pixelSize: Theme.fontSize
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: win.info.userHost || ""
                        color: Theme.muted
                        font.pixelSize: Theme.small
                        elide: Text.ElideMiddle
                    }
                }
            }

            // Status: asking, or an error.
            Text {
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.fillWidth: true
                visible: text.length > 0
                text: win.info.error ? win.info.error
                    : win.info.loading && (win.info.rows || []).length === 0 ? qsTr("Asking %1…").arg(win.info.server || qsTr("the server"))
                    : ""
                color: win.info.error ? Theme.danger : Theme.muted
                font.pixelSize: Theme.small
                wrapMode: Text.Wrap
            }

            // The details, label / value.
            Rectangle {
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.fillWidth: true
                visible: (win.info.rows || []).length > 0
                implicitHeight: rows.implicitHeight + 16
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.border
                GridLayout {
                    id: rows
                    anchors.fill: parent
                    anchors.margins: 8
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    columns: 2
                    columnSpacing: 14
                    rowSpacing: 8
                    Repeater {
                        model: win.info.rows || []
                        delegate: Text {
                            required property var modelData
                            required property int index
                            Layout.row: index
                            Layout.column: 0
                            Layout.alignment: Qt.AlignTop
                            text: modelData.label
                            color: Theme.muted
                            font.pixelSize: Theme.small
                            font.weight: Font.DemiBold
                        }
                    }
                    Repeater {
                        model: win.info.rows || []
                        delegate: TextEdit {
                            required property var modelData
                            required property int index
                            Layout.row: index
                            Layout.column: 1
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1 // wrap to the column instead of widening it
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            textFormat: TextEdit.PlainText
                            text: modelData.text
                            color: Theme.text
                            font.pixelSize: Theme.small
                            selectionColor: Theme.alpha(Theme.accent, 0.35)
                        }
                    }
                }
            }

            RowLayout {
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 16
                Layout.fillWidth: true
                spacing: 8
                AppButton {
                    primary: true
                    text: qsTr("Send message")
                    enabled: !win.info.error
                    onClicked: { Relay.openQuery(win.nick); win.close() }
                }
                AppButton {
                    text: qsTr("Mention")
                    onClicked: { win.mention(win.nick); win.close() }
                }
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("Refresh")
                    enabled: !win.info.loading
                    onClicked: Relay.requestWhois(win.nick)
                }
            }
        }
    }
}
