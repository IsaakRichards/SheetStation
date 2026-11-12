import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    allowedOrientations: Orientation.All

    SilicaListView {
        id: list
        anchors.fill: parent
        model: station.transfers
        header: PageHeader { title: qsTr("Transfers") }

        PullDownMenu {
            MenuItem { text: qsTr("Open download folder"); onClicked: station.openPath(station.downloadDir) }
            MenuItem { text: qsTr("Clear finished"); onClicked: station.clearFinished() }
        }

        ViewPlaceholder { enabled: list.count === 0; text: qsTr("No transfers yet") }

        delegate: ListItem {
            contentHeight: col.height + 2 * Theme.paddingSmall
            onClicked: if (modelData.state === "done" && modelData.path) station.openPath(modelData.path)
            Column {
                id: col
                anchors { left: parent.left; right: parent.right; margins: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
                Label {
                    width: parent.width
                    text: (modelData.direction === "down" ? "⬇ " : "⬆ ") + modelData.name
                    truncationMode: TruncationMode.Fade
                    color: highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                ProgressBar {
                    width: parent.width
                    leftMargin: 0; rightMargin: 0
                    visible: modelData.state === "running" || modelData.state === "waiting"
                    value: modelData.progress
                    indeterminate: modelData.state === "waiting"
                }
                Label {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: modelData.state === "failed" ? Theme.errorColor : Theme.secondaryColor
                    text: (modelData.direction === "down" ? qsTr("from %1") : qsTr("to %1")).arg(modelData.peerName)
                          + " · " + station.formatSize(modelData.bytes) + " / " + station.formatSize(modelData.total)
                          + " · " + modelData.state + (modelData.error ? " · " + modelData.error : "")
                }
            }
        }
        VerticalScrollDecorator {}
    }
}
