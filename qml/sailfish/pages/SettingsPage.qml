import QtQuick 2.0
import Sailfish.Silica 1.0

Dialog {
    allowedOrientations: Orientation.All
    onAccepted: {
        station.deviceName = nameField.text
        station.shareDir = shareField.text
        station.downloadDir = dlField.text
        station.stunServers = stunField.text
        station.scriptUrl = urlField.text
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height
        Column {
            id: col
            width: parent.width
            DialogHeader { acceptText: qsTr("Save"); title: qsTr("Settings") }
            TextField {
                id: urlField
                width: parent.width
                label: qsTr("Apps Script web app URL")
                placeholderText: label
                text: station.scriptUrl
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            }
            TextField { id: nameField; width: parent.width; label: qsTr("Device name"); placeholderText: label; text: station.deviceName }
            TextField { id: shareField; width: parent.width; label: qsTr("Shared folder"); placeholderText: label; text: station.shareDir }
            TextField { id: dlField; width: parent.width; label: qsTr("Download folder"); placeholderText: label; text: station.downloadDir }
            TextField {
                id: stunField
                width: parent.width
                label: qsTr("STUN servers")
                placeholderText: qsTr("empty = LAN / IPv6 only")
                text: station.stunServers
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("The STUN server only tells this device its public address so devices on different networks can connect. "
                         + "No files go through it. Clear it to connect only on the same network or over IPv6.")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
                text: qsTr("Device id: %1").arg(station.deviceId)
                wrapMode: Text.WrapAnywhere
            }
        }
    }
}
