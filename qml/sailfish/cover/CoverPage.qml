import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    Column {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        spacing: Theme.paddingMedium
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: "SheetStation"
            color: Theme.highlightColor
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontSizeSmall
            text: station.activeTransfers > 0 ? qsTr("%1 transfers").arg(station.activeTransfers)
                                              : qsTr("%1 devices").arg(onlineCount())
            function onlineCount() {
                var n = 0
                for (var i = 0; i < station.peers.length; i++) if (station.peers[i].online) n++
                return n
            }
        }
    }
    CoverActionList {
        CoverAction { iconSource: "image://theme/icon-cover-refresh"; onTriggered: station.refresh() }
    }
}
