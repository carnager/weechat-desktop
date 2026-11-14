import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import WeechatDesktop.Core

// Searchable emoji grid with category tabs and recently used emoji. Fully keyboard driven:
// type to search, arrows to move, Enter to insert (Shift+Enter keeps the picker open).
Popup {
    id: picker
    signal picked(string emoji)

    property int group: -1           // -1 = recent
    property var entries: []
    property int current: 0
    readonly property int columns: 9

    function openPicker() {
        search.text = ""
        group = Emoji.recent.length > 0 ? -1 : 0
        reload()
        open()
        search.forceActiveFocus()
    }
    function reload() {
        if (search.text.length > 0)
            entries = Emoji.search(search.text, 90)
        else
            entries = group < 0 ? Emoji.recent : Emoji.group(group)
        current = 0
        grid.positionViewAtBeginning()
    }
    function choose(i, keepOpen) {
        if (i < 0 || i >= entries.length) return
        picked(entries[i].emoji)
        if (!keepOpen) close()
    }
    function move(d) {
        if (entries.length === 0) return
        current = Math.max(0, Math.min(entries.length - 1, current + d))
        grid.positionViewAtIndex(current, GridView.Contain)
    }

    width: columns * 40 + 26
    height: 400
    padding: 10
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
    }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.anim }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.anim; easing.type: Easing.OutCubic }
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        AppTextField {
            id: search
            Layout.fillWidth: true
            leadingIcon: "search"
            placeholderText: qsTr("Search emoji")
            implicitHeight: 34
            onTextChanged: picker.reload()
            Keys.onPressed: (e) => {
                switch (e.key) {
                case Qt.Key_Left: if (search.text.length === 0 || search.cursorPosition === 0) { picker.move(-1); e.accepted = true } break
                case Qt.Key_Right: if (search.text.length === 0 || search.cursorPosition === search.text.length) { picker.move(1); e.accepted = true } break
                case Qt.Key_Up: picker.move(-picker.columns); e.accepted = true; break
                case Qt.Key_Down: picker.move(picker.columns); e.accepted = true; break
                case Qt.Key_Return:
                case Qt.Key_Enter: picker.choose(picker.current, e.modifiers & Qt.ShiftModifier); e.accepted = true; break
                case Qt.Key_Tab:
                    picker.group = (picker.group + 2) % (Emoji.groups.length + 1) - 1
                    search.text = ""; picker.reload(); e.accepted = true; break
                case Qt.Key_Backtab:
                    picker.group = (picker.group + Emoji.groups.length + 1) % (Emoji.groups.length + 1) - 1
                    search.text = ""; picker.reload(); e.accepted = true; break
                }
            }
        }

        // Category tabs
        Row {
            Layout.fillWidth: true
            visible: search.text.length === 0
            spacing: 0
            Repeater {
                model: [""].concat(Emoji.groupIcons)
                delegate: AbstractButton {
                    required property string modelData
                    required property int index
                    readonly property int g: index - 1
                    width: (picker.availableWidth) / (Emoji.groupIcons.length + 1)
                    height: 30
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    onClicked: { picker.group = g; picker.reload(); search.forceActiveFocus() }
                    background: Rectangle {
                        radius: Theme.radius - 2
                        color: picker.group === parent.g ? Theme.accentSoft : (parent.hovered ? Theme.hover : "transparent")
                    }
                    contentItem: Item {
                        Icon { visible: parent.parent.g < 0; anchors.centerIn: parent; name: "clock"; size: 16; color: Theme.muted }
                        Text {
                            visible: parent.parent.g >= 0
                            anchors.centerIn: parent
                            text: parent.parent.modelData
                            font.pixelSize: 17
                        }
                    }
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: g < 0 ? qsTr("Recently used") : Emoji.groups[g]
                }
            }
        }

        Text {
            text: search.text.length > 0 ? qsTr("Results") : picker.group < 0 ? qsTr("Recently used") : Emoji.groups[picker.group]
            color: Theme.faint
            font.pixelSize: Theme.tiny
            font.weight: Font.Bold
        }

        GridView {
            acceptedButtons: Qt.NoButton // desktop: no mouse-drag flicking (touch still works)
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: Math.floor(width / picker.columns)
            cellHeight: 40
            model: picker.entries
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}
            delegate: AbstractButton {
                id: cell
                required property var modelData
                required property int index
                width: grid.cellWidth
                height: grid.cellHeight
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                onHoveredChanged: if (hovered) picker.current = index
                onClicked: picker.choose(index, false)
                background: Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: Theme.radius
                    color: picker.current === cell.index ? Theme.accentSoft : "transparent"
                }
                contentItem: Text {
                    text: cell.modelData.emoji
                    font.pixelSize: 24
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Text {
                anchors.centerIn: parent
                visible: picker.entries.length === 0
                text: search.text.length > 0 ? qsTr("No emoji found") : qsTr("Nothing here yet")
                color: Theme.faint
                font.pixelSize: Theme.small
            }
        }

        // Preview footer
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            readonly property var sel: picker.entries[picker.current]
            Text {
                text: parent.sel ? parent.sel.emoji : ""
                font.pixelSize: 26
            }
            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                Text {
                    text: parent.parent.sel ? parent.parent.sel.name : ""
                    color: Theme.text
                    font.pixelSize: Theme.small
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: parent.parent.sel ? ":" + parent.parent.sel.shortcode + ":" : ""
                    color: Theme.muted
                    font.pixelSize: Theme.tiny
                }
            }
        }
    }
}
