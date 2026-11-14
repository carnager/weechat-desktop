import QtQuick
import QtQuick.Layouts
import WeechatDesktop.Core

// An inline image, or an OpenGraph card for a page link.
Item {
    id: preview
    required property string url
    property real maxWidth: 480
    property var info: Previews.get(url)

    readonly property bool isImage: info.status === "ready" && info.type === "image" && AppSettings.inlineImages
    readonly property bool isCard: info.status === "ready" && info.type === "page" && AppSettings.linkPreviews
                                   && (info.title || info.description)

    readonly property var image: imgFrame.img
    readonly property int imageStatus: image ? image.status : Image.Null
    width: isImage ? Math.min(maxWidth, imageStatus === Image.Ready ? image.width : 240)
                   : isCard ? Math.min(maxWidth, 460) : 0
    height: isImage ? (imageStatus === Image.Ready ? image.height + 4 : (imageStatus === Image.Error ? 0 : 124))
                    : isCard ? card.implicitHeight + 4 : 0
    visible: height > 0

    Connections {
        target: Previews
        function onUpdated(u) { if (u === preview.url) preview.info = Previews.get(preview.url) }
    }

    // ---- image
    Rectangle {
        id: imgFrame
        visible: preview.isImage
        anchors.fill: parent
        anchors.topMargin: 4
        radius: Theme.radius
        color: Theme.raised
        clip: true
        // Animated formats need AnimatedImage (GIFs play on hover); everything else is a plain Image.
        Loader {
            id: imgLoader
            anchors.left: parent.left
            anchors.top: parent.top
            active: preview.isImage
            sourceComponent: /\.(gif|webp)(\?|#|$)/i.test(preview.info.image || "") ? animated : still
        }
        Component {
            id: still
            Image {
                source: preview.info.image
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectFit
                sourceSize.width: Math.round(preview.maxWidth * 2)
                width: Math.min(preview.maxWidth, implicitWidth / 2 || preview.maxWidth)
                height: Math.min(320, implicitHeight > 0 ? (implicitHeight / 2) * width / Math.max(1, implicitWidth / 2) : 320)
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: Qt.openUrlExternally(preview.url) }
            }
        }
        Component {
            id: animated
            AnimatedImage {
                source: preview.info.image
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectFit
                width: Math.min(preview.maxWidth, implicitWidth || preview.maxWidth)
                height: Math.min(320, implicitHeight > 0 ? implicitHeight * width / Math.max(1, implicitWidth) : 320)
                playing: hoverImg.hovered || frameCount < 2
                HoverHandler { id: hoverImg; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: Qt.openUrlExternally(preview.url) }
                Rectangle {
                    visible: parent.frameCount > 1 && !parent.playing
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 6
                    width: 34; height: 18; radius: 4
                    color: "#99000000"
                    Text { anchors.centerIn: parent; text: "GIF"; color: "white"; font.pixelSize: 10; font.weight: Font.Bold }
                }
            }
        }
        readonly property var img: imgLoader.item
        Text {
            visible: !!parent.img && parent.img.status === Image.Loading
            anchors.centerIn: parent
            text: qsTr("Loading image…")
            color: Theme.faint
            font.pixelSize: Theme.tiny
        }
    }

    // ---- page card
    Rectangle {
        id: card
        visible: preview.isCard
        width: parent.width
        y: 4
        implicitHeight: cardRow.implicitHeight + 20
        radius: Theme.radius
        color: Theme.dark ? Theme.alpha("#000000", 0.18) : Theme.raised
        border.width: 1
        border.color: Theme.border
        Rectangle { width: 3; height: parent.height - 16; y: 8; x: 0; radius: 2; color: Theme.accent }

        RowLayout {
            id: cardRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 10
            anchors.leftMargin: 14
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    Layout.fillWidth: true
                    text: preview.info.site || ""
                    color: Theme.faint
                    font.pixelSize: Theme.tiny
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: preview.info.title || ""
                    color: Theme.link
                    font.pixelSize: Theme.small + 1
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Qt.openUrlExternally(preview.url)
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: preview.info.description || ""
                    color: Theme.muted
                    font.pixelSize: Theme.small
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
            }
            Image {
                visible: status === Image.Ready && AppSettings.inlineImages
                source: preview.isCard && preview.info.image ? preview.info.image : ""
                asynchronous: true
                fillMode: Image.PreserveAspectCrop
                Layout.preferredWidth: 72
                Layout.preferredHeight: 72
                Layout.alignment: Qt.AlignTop
                sourceSize: Qt.size(160, 160)
                layer.enabled: false
            }
        }
    }
}
