import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    allowedOrientations: Orientation.All
    SilicaListView {
        id: list
        anchors.fill: parent
        model: station.myFiles
        header: Column {
            width: list.width
            PageHeader { title: qsTr("My shared files"); description: station.shareDir }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.errorColor
                text: qsTr("Anyone with your sheet URL can download these. Don't put sensitive files here.")
            }
        }
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
