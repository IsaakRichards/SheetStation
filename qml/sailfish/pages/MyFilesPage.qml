import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    allowedOrientations: Orientation.All
    SilicaListView {
        id: list
        anchors.fill: parent
        model: station.myFiles
        header: PageHeader { title: qsTr("My shared files"); description: station.shareDir }
        PullDownMenu {
            MenuItem { text: qsTr("Open shared folder"); onClicked: station.openPath(station.shareDir) }
            MenuItem { text: qsTr("Rescan"); onClicked: station.refresh() }
        }
        ViewPlaceholder {
            enabled: list.count === 0
            text: qsTr("Nothing shared")
            hintText: qsTr("Copy files into the shared folder")
        }
        delegate: ListItem {
            contentHeight: Theme.itemSizeSmall
            Label {
                anchors { left: parent.left; right: parent.right; margins: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
                text: modelData.name + "  (" + station.formatSize(modelData.size) + ")"
                truncationMode: TruncationMode.Fade
            }
        }
        VerticalScrollDecorator {}
    }
}
