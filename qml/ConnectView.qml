import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import WeechatDesktop.Core

// First-run / edit-connection screen.
Rectangle {
    id: view
    signal done()
    signal cancelled()
    property bool cancellable: false
    color: Theme.window

    // Accept a pasted URL: "wss://host:port/api" fills in TLS and the port, and leaves the host.
    function applyUrl() {
        const m = host.text.trim().match(/^(?:(wss|https|ws|http):\/\/)?([^\/:]+|\[[^\]]+\])(?::(\d+))?(?:\/.*)?$/i)
        if (!m || (!m[1] && !m[3] && host.text.trim() === m[2]))
            return
        if (m[1]) {
            const secure = /^(wss|https)$/i.test(m[1])
            tls.checked = secure
            if (!m[3] && secure)
                port.text = "443"
        }
        if (m[3])
            port.text = m[3]
        host.text = m[2]
    }

    function connectNow() {
        applyUrl()
        AppSettings.host = host.text.trim()
        AppSettings.port = parseInt(port.text) || 9000
        AppSettings.tls = tls.checked
        AppSettings.password = password.text
        Relay.connectToRelay()
        waiting = true
    }
    property bool waiting: false

    Connections {
        target: Relay
        function onSyncedChanged() {
            if (view.waiting && Relay.synced) {
                view.waiting = false
                view.done()
            }
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(440, parent.width - 40)
        height: form.implicitHeight + 56
        radius: 18
        color: Theme.surface
        border.width: 1
        border.color: Theme.border

        ColumnLayout {
            id: form
            anchors.fill: parent
            anchors.margins: 28
            spacing: 14

            Image {
                Layout.alignment: Qt.AlignHCenter
                source: "qrc:/resources/icon.svg"
                sourceSize: Qt.size(128, 128)
                Layout.preferredWidth: 64
                Layout.preferredHeight: 64
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Connect to WeeChat")
                color: Theme.text
                font.pixelSize: Theme.title
                font.weight: Font.Bold
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: qsTr("Needs WeeChat 4.0+ with an api relay:") + "\n/relay add tls.api 9000"
                color: Theme.muted
                font.pixelSize: Theme.small
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Text { text: qsTr("Host"); color: Theme.muted; font.pixelSize: Theme.small; font.weight: Font.DemiBold }
                RowLayout {
                    spacing: 8
                    AppTextField {
                        id: host
                        Layout.fillWidth: true
                        text: AppSettings.host
                        placeholderText: "weechat.example.org"
                        focus: true
                        KeyNavigation.tab: port
                        onAccepted: view.connectNow()
                        onEditingFinished: view.applyUrl()
                    }
                    AppTextField {
                        id: port
                        Layout.preferredWidth: 90
                        text: AppSettings.port
                        validator: IntValidator { bottom: 1; top: 65535 }
                        placeholderText: "9000"
                        KeyNavigation.tab: password
                        onAccepted: view.connectNow()
                        // 443 is HTTPS: a relay there is behind a TLS reverse proxy.
                        onTextEdited: if (text === "443") tls.checked = true
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Text { text: qsTr("Relay password"); color: Theme.muted; font.pixelSize: Theme.small; font.weight: Font.DemiBold }
                AppTextField {
                    id: password
                    Layout.fillWidth: true
                    text: AppSettings.password
                    echoMode: TextInput.Password
                    leadingIcon: "lock"
                    KeyNavigation.tab: tls
                    onAccepted: view.connectNow()
                }
            }
            AppSwitch {
                id: tls
                text: qsTr("Use TLS (wss://)")
                description: qsTr("Self-signed certificates can be trusted after the first attempt.")
                checked: AppSettings.tls
                Layout.leftMargin: -10
                Layout.rightMargin: -10
                KeyNavigation.tab: connectBtn
            }

            Rectangle {
                Layout.fillWidth: true
                visible: (Relay.state === 3 || Relay.state === 4) && Relay.errorString.length > 0
                implicitHeight: err.implicitHeight + 16
                radius: Theme.radius
                color: Theme.alpha(Theme.danger, 0.12)
                Text {
                    id: err
                    anchors.fill: parent
                    anchors.margins: 8
                    text: Relay.errorString
                    color: Theme.danger
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.small
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: view.waiting && Relay.state !== 4 ? Relay.stateText : ""
                    color: Theme.muted
                    font.pixelSize: Theme.small
                }
                AppButton {
                    visible: view.cancellable
                    text: qsTr("Cancel")
                    onClicked: view.cancelled()
                }
                AppButton {
                    id: connectBtn
                    primary: true
                    text: qsTr("Connect")
                    enabled: host.text.trim().length > 0
                    onClicked: view.connectNow()
                    Keys.onReturnPressed: view.connectNow()
                }
            }
        }
    }
}
