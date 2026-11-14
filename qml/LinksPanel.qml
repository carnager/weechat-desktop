import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import WeechatDesktop.Core

// The links posted in the current buffer (as far as its history is loaded), newest first. Each
// has a hint label: typing it opens the link, with Shift copies it. "/" filters.
Popup {
    id: panel
    signal jump(var lineId)
    signal toast(string message)

    property var all: []
    property string hintTyped: ""
    readonly property var shown: {
        const q = filter.text.trim().toLowerCase()
        const list = q.length === 0 ? all : all.filter(l =>
            l.url.toLowerCase().includes(q) || l.nick.toLowerCase().includes(q))
        const labels = chatHints(list.length)
        return list.map((l, i) => Object.assign({ label: labels[i] }, l))
    }
    // Same labels as the on-screen hints (ChatView.hintLabels).
    function chatHints(n) {
        const keys = "asdfjklghqweruiopzxcvnmtyb"
        const out = []
        if (n <= keys.length) {
            for (let i = 0; i < n; ++i) out.push(keys[i])
        } else {
            for (let i = 0; i < keys.length && out.length < n; ++i)
                for (let j = 0; j < keys.length && out.length < n; ++j)
                    out.push(keys[i] + keys[j])
        }
        return out
    }

    function openFor(buffer) {
        all = buffer ? buffer.lines.links() : []
        filter.text = ""
        hintTyped = ""
        open()
    }
    function activate(entry, copy) {
        if (!entry)
            return
        if (copy) {
            Platform.copy(entry.url)
            panel.toast(qsTr("Link copied"))
        } else {
            Qt.openUrlExternally(entry.url)
        }
        close()
    }
    function typeHint(key, copy) {
        const typed = hintTyped + key.toLowerCase()
        const matching = shown.filter(l => l.label.startsWith(typed))
        if (matching.length === 0)
            return
        const exact = matching.find(l => l.label === typed)
        if (exact)
            activate(exact, copy)
        else
            hintTyped = typed
    }

    parent: Overlay.overlay
    x: parent.width - width - 12
    y: 12
    width: Math.min(520, parent.width - 24)
    height: parent.height - 24
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Theme.alpha("#000000", Theme.dark ? 0.35 : 0.18) }
    background: Rectangle { radius: Theme.radiusLarge; color: Theme.surface; border.width: 1; border.color: Theme.border }
    enter: Transition { NumberAnimation { property: "x"; from: panel.parent.width; to: panel.parent.width - panel.width - 12; duration: Theme.anim; easing.type: Easing.OutCubic } }

    onOpened: list.forceActiveFocus()

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            Layout.leftMargin: 18
            Layout.rightMargin: 10
            spacing: 8
            Icon { name: "external"; size: 18; color: Theme.accent }
            Text {
                text: qsTr("Links")
                color: Theme.text
                font.pixelSize: Theme.large
                font.weight: Font.Bold
            }
            Text {
                Layout.fillWidth: true
                text: Relay.current ? Relay.current.name : ""
                color: Theme.muted
                font.pixelSize: Theme.small
                elide: Text.ElideRight
            }
            IconButton { iconName: "x"; tip: qsTr("Close (Esc)"); onClicked: panel.close() }
        }

        AppTextField {
            id: filter
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.bottomMargin: 10
            implicitHeight: 34
            leadingIcon: "search"
            placeholderText: qsTr("Filter by address or nick  ( / )")
            onTextChanged: { panel.hintTyped = ""; list.currentIndex = 0 }
            Keys.onDownPressed: list.forceActiveFocus()
            Keys.onReturnPressed: (e) => panel.activate(panel.shown[Math.max(0, list.currentIndex)], e.modifiers & Qt.ShiftModifier)
            Keys.onEnterPressed: (e) => panel.activate(panel.shown[Math.max(0, list.currentIndex)], e.modifiers & Qt.ShiftModifier)
            Keys.onEscapePressed: (e) => {
                if (text.length) text = ""
                else panel.close()
                e.accepted = true
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        ListView {
            acceptedButtons: Qt.NoButton // desktop: no mouse-drag flicking (touch still works)
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: panel.shown
            currentIndex: 0
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}
            Keys.onPressed: (e) => {
                if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                    panel.activate(panel.shown[currentIndex], e.modifiers & Qt.ShiftModifier)
                } else if (e.matches(StandardKey.Copy)) {
                    panel.activate(panel.shown[currentIndex], true)
                } else if (e.text === "/") {
                    filter.forceActiveFocus()
                } else if (e.key === Qt.Key_Backspace) {
                    panel.hintTyped = panel.hintTyped.slice(0, -1)
                } else if (e.text.length === 1 && /[a-z]/i.test(e.text) && !(e.modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
                    panel.typeHint(e.text, (e.modifiers & Qt.ShiftModifier) !== 0)
                } else {
                    return
                }
                e.accepted = true
            }

            delegate: AbstractButton {
                id: row
                required property var modelData
                required property int index
                readonly property bool live: modelData.label.startsWith(panel.hintTyped)
                width: ListView.view.width
                topPadding: 9
                bottomPadding: 10
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                opacity: live ? 1 : 0.35
                readonly property bool current: ListView.isCurrentItem
                onHoveredChanged: if (hovered) row.ListView.view.currentIndex = index
                onClicked: panel.activate(modelData, false)
                // The current row (Enter opens it): accent tint and a bar, like the quick switcher.
                background: Rectangle {
                    color: row.current ? Theme.accentSoft : "transparent"
                    Rectangle { visible: row.current; width: 3; height: parent.height; color: Theme.accent }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
                }
                contentItem: RowLayout {
                    spacing: 12
                    Rectangle {
                        Layout.leftMargin: 14
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 1
                        implicitWidth: Math.max(26, hintText.implicitWidth + 10)
                        implicitHeight: hintText.implicitHeight + 4
                        radius: 4
                        color: row.live ? Theme.accent : Theme.alpha(Theme.muted, 0.3)
                        Text {
                            id: hintText
                            anchors.centerIn: parent
                            textFormat: Text.StyledText
                            text: "<span style=\"opacity:0.55\">" + (row.live ? panel.hintTyped : "") + "</span>"
                                  + row.modelData.label.substring(row.live ? panel.hintTyped.length : 0)
                            color: Theme.onAccent
                            font.pixelSize: Theme.small
                            font.weight: Font.Bold
                            font.family: "monospace"
                            font.capitalization: Font.AllUppercase
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.url.replace(/^https?:\/\//, "")
                            color: row.current ? Theme.text : Theme.link
                            font.weight: row.current ? Font.DemiBold : Font.Normal
                            font.pixelSize: Theme.small + 1
                            elide: Text.ElideMiddle
                        }
                        Text {
                            Layout.fillWidth: true
                            text: {
                                const m = row.modelData
                                let t = (m.nick ? m.nick + " · " : "") + Qt.formatDateTime(m.date, "MMM d, HH:mm")
                                if (m.count > 1)
                                    t += " · " + qsTr("posted %n times", "", m.count)
                                return t
                            }
                            color: Theme.faint
                            font.pixelSize: Theme.tiny
                            elide: Text.ElideRight
                        }
                    }
                    IconButton {
                        visible: row.current
                        iconName: "copy"; iconSize: 15; implicitWidth: 28; implicitHeight: 28
                        tip: qsTr("Copy (Shift + label)")
                        onClicked: panel.activate(row.modelData, true)
                    }
                    IconButton {
                        Layout.rightMargin: 8
                        visible: row.current
                        iconName: "message"; iconSize: 15; implicitWidth: 28; implicitHeight: 28
                        tip: qsTr("Show in chat")
                        onClicked: { panel.jump(row.modelData.lineId); panel.close() }
                    }
                }
            }
            Column {
                anchors.centerIn: parent
                visible: panel.shown.length === 0
                spacing: 8
                Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "external"; size: 32; color: Theme.faint }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: panel.all.length === 0 ? qsTr("No links here yet") : qsTr("No links match")
                    color: Theme.muted
                    font.pixelSize: Theme.fontSize
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: panel.all.length === 0
                    text: qsTr("Links from the loaded history of this buffer show up here.")
                    color: Theme.faint
                    font.pixelSize: Theme.small
                }
            }
        }

        // Keys, at the bottom.
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
        Text {
            Layout.fillWidth: true
            Layout.margins: 10
            Layout.leftMargin: 18
            text: qsTr("Label or Enter: open · Shift: copy · /: filter · Esc: close")
            color: Theme.faint
            font.pixelSize: Theme.tiny
            elide: Text.ElideRight
        }
    }
}
