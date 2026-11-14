import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import WeechatDesktop.Core

Rectangle {
    id: panel
    property var buffer: null
    signal nickClicked(string nick, Item item)
    signal nickActivated(string nick)

    color: Theme.sidebar
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            Layout.leftMargin: 16
            Layout.rightMargin: 8
            Text {
                Layout.fillWidth: true
                text: qsTr("Members")
                color: Theme.text
                font.pixelSize: Theme.fontSize
                font.weight: Font.Bold
            }
            Text {
                text: panel.buffer ? panel.buffer.nicks.count : ""
                color: Theme.faint
                font.pixelSize: Theme.small
            }
            IconButton {
                iconName: "x"
                iconSize: 15
                tip: qsTr("Hide (Ctrl+Shift+N)")
                onClicked: AppSettings.showNicklist = false
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        AppTextField {
            id: filter
            Layout.fillWidth: true
            Layout.margins: 10
            implicitHeight: 32
            leadingIcon: "search"
            placeholderText: qsTr("Filter")
            visible: panel.buffer && panel.buffer.nicks.count > 12
            Keys.onEscapePressed: text = ""
        }

        ListView {
            acceptedButtons: Qt.NoButton // desktop: no mouse-drag flicking (touch still works)
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: panel.buffer ? panel.buffer.nicks : null
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            topMargin: 4
            bottomMargin: 8
            ScrollBar.vertical: AppScrollBar {}
            section.property: "section"
            section.delegate: Text {
                required property string section
                width: ListView.view.width
                leftPadding: 16
                topPadding: 12
                bottomPadding: 4
                text: section.toUpperCase()
                color: Theme.faint
                font.pixelSize: Theme.tiny
                font.weight: Font.Bold
                font.letterSpacing: 0.8
                visible: filter.text.length === 0
                height: visible ? implicitHeight : 0
            }
            delegate: AbstractButton {
                id: nickRow
                required property string name
                required property string prefix
                required property color nickColor
                readonly property bool matches: filter.text.length === 0 || name.toLowerCase().indexOf(filter.text.toLowerCase()) >= 0
                width: ListView.view.width
                height: matches ? 30 : 0
                visible: matches
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                onClicked: panel.nickClicked(name, nickRow)
                onDoubleClicked: panel.nickActivated(name)
                background: Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    radius: Theme.radius
                    color: nickRow.hovered ? Theme.hover : "transparent"
                }
                contentItem: RowLayout {
                    spacing: 8
                    Item { implicitWidth: 6 }
                    Avatar { nick: nickRow.name; tint: nickRow.nickColor; size: 22 }
                    Text {
                        text: nickRow.prefix
                        visible: nickRow.prefix.length > 0
                        color: Theme.faint
                        font.pixelSize: Theme.small
                        font.weight: Font.Bold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: nickRow.name
                        color: nickRow.nickColor
                        font.pixelSize: Theme.small + 1
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
