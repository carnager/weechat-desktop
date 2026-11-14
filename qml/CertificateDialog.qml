import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import WeechatDesktop.Core

Popup {
    id: dlg
    readonly property var cert: Relay.untrustedCertificate

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(520, parent.width - 40)
    modal: true
    focus: true
    padding: 24
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: Theme.alpha("#000000", 0.45) }
    background: Rectangle { radius: Theme.radiusLarge; color: Theme.surface; border.width: 1; border.color: Theme.border }

    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            spacing: 10
            Icon { name: "shield"; size: 24; color: Theme.warn }
            Text {
                text: dlg.cert.replacesPinned ? qsTr("The relay's certificate changed") : qsTr("Untrusted certificate")
                color: Theme.text
                font.pixelSize: Theme.large
                font.weight: Font.Bold
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.muted
            font.pixelSize: Theme.small
            text: dlg.cert.replacesPinned
                  ? qsTr("This is not the certificate you trusted before. If you didn't replace it yourself, someone may be intercepting the connection.")
                  : qsTr("%1 presented a certificate your system doesn't trust (common for self-signed relays). Only trust it if the fingerprint matches the one on your server.").arg(dlg.cert.host || "")
        }
        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 6
            Layout.fillWidth: true
            Text { text: qsTr("Subject"); color: Theme.faint; font.pixelSize: Theme.small }
            Text { text: dlg.cert.subject || "—"; color: Theme.text; font.pixelSize: Theme.small; Layout.fillWidth: true; elide: Text.ElideRight }
            Text { text: qsTr("Issuer"); color: Theme.faint; font.pixelSize: Theme.small }
            Text { text: dlg.cert.issuer || "—"; color: Theme.text; font.pixelSize: Theme.small; Layout.fillWidth: true; elide: Text.ElideRight }
            Text { text: qsTr("Expires"); color: Theme.faint; font.pixelSize: Theme.small }
            Text { text: dlg.cert.notAfter || "—"; color: Theme.text; font.pixelSize: Theme.small }
            Text { text: qsTr("Problem"); color: Theme.faint; font.pixelSize: Theme.small; Layout.alignment: Qt.AlignTop }
            Text { text: dlg.cert.reasons || "—"; color: Theme.text; font.pixelSize: Theme.small; Layout.fillWidth: true; wrapMode: Text.Wrap }
        }
        Text { text: qsTr("SHA-256 fingerprint"); color: Theme.faint; font.pixelSize: Theme.small }
        TextEdit {
            Layout.fillWidth: true
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.WrapAnywhere
            text: dlg.cert.sha256 || ""
            color: Theme.text
            font.family: Theme.monoFamily
            font.pixelSize: Theme.small
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.faint
            font.pixelSize: Theme.tiny
            text: qsTr("Compare with: openssl x509 -in relay.pem -noout -fingerprint -sha256")
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Cancel"); onClicked: dlg.close() }
            AppButton {
                primary: true
                text: qsTr("Trust and connect")
                onClicked: { Relay.trustCertificate(dlg.cert.sha256); dlg.close() }
            }
        }
    }
}
