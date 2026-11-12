import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    allowedOrientations: Orientation.All

    Component.onCompleted: if (!station.configured) pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))

    SilicaListView {
        id: list
        anchors.fill: parent
        model: station.peers

        PullDownMenu {
            MenuItem { text: qsTr("Settings"); onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml")) }
            MenuItem { text: qsTr("My shared files"); onClicked: pageStack.push(Qt.resolvedUrl("MyFilesPage.qml")) }
            MenuItem {
                text: station.activeTransfers > 0 ? qsTr("Transfers (%1)").arg(station.activeTransfers) : qsTr("Transfers")
                onClicked: pageStack.push(Qt.resolvedUrl("TransfersPage.qml"))
            }
            MenuItem { text: qsTr("Refresh"); onClicked: station.refresh() }
        }

        header: PageHeader {
            title: qsTr("Devices")
            description: station.status
        }

        ViewPlaceholder {
            enabled: list.count === 0
            text: station.configured ? qsTr("No other devices yet") : qsTr("Not set up")
            hintText: station.configured ? qsTr("Open SheetStation on another device with the same sheet URL")
                                         : qsTr("Pull down and open Settings")
        }

        delegate: ListItem {
            contentHeight: Theme.itemSizeMedium
            opacity: modelData.online ? 1.0 : 0.5
            onClicked: pageStack.push(Qt.resolvedUrl("PeerPage.qml"), { peerId: modelData.id })

            Rectangle {
                id: dot
                width: Theme.paddingMedium; height: width; radius: width / 2
                anchors { left: parent.left; leftMargin: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
                color: modelData.link === "connected" ? Theme.highlightColor : (modelData.online ? "#3cb371" : Theme.secondaryColor)
            }
            Column {
                anchors { left: dot.right; leftMargin: Theme.paddingMedium; right: parent.right
                          rightMargin: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
                Label {
                    width: parent.width
                    text: modelData.name
                    truncationMode: TruncationMode.Fade
                    color: highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    width: parent.width
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: modelData.platform + " · " + qsTr("%1 files").arg(modelData.files.length)
                          + (modelData.link === "connecting" ? " · " + qsTr("connecting...") : "")
                          + (modelData.online ? "" : " · " + qsTr("offline"))
                }
            }
        }
        VerticalScrollDecorator {}
    }
}
