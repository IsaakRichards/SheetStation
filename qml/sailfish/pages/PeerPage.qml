import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    property string peerId
    property var peer: findPeer(station.peers)
    allowedOrientations: Orientation.All

    function findPeer(list) {
        for (var i = 0; i < list.length; i++) if (list[i].id === peerId) return list[i]
        return null
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: peer ? peer.files : []
        header: Column {
            width: list.width
            PageHeader {
                title: peer ? peer.name : ""
                description: peer ? (peer.online ? peer.platform : qsTr("offline")) : ""
            }
            SearchField {
                id: search
                width: parent.width
                placeholderText: qsTr("Filter")
                onTextChanged: list.filter = text.toLowerCase()
            }
        }
        property string filter: ""

        ViewPlaceholder { enabled: list.count === 0; text: qsTr("Nothing shared") }

        delegate: ListItem {
            visible: list.filter.length === 0 || modelData.name.toLowerCase().indexOf(list.filter) >= 0
            contentHeight: visible ? Theme.itemSizeSmall : 0
            enabled: peer && peer.online
            onClicked: {
                station.download(peerId, modelData.name)
                pageStack.push(Qt.resolvedUrl("TransfersPage.qml"))
            }
            Label {
                anchors { left: parent.left; right: sizeLabel.left; leftMargin: Theme.horizontalPageMargin
                          rightMargin: Theme.paddingMedium; verticalCenter: parent.verticalCenter }
                text: modelData.name
                truncationMode: TruncationMode.Fade
                color: highlighted ? Theme.highlightColor : Theme.primaryColor
            }
            Label {
                id: sizeLabel
                anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
                text: station.formatSize(modelData.size)
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
            }
        }
        VerticalScrollDecorator {}
    }
}
