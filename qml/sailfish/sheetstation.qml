import QtQuick 2.0
import Sailfish.Silica 1.0
import "pages"
import "cover"

ApplicationWindow {
    initialPage: Component { MainPage {} }
    cover: Component { CoverPage {} }
    allowedOrientations: defaultAllowedOrientations

    Connections {
        target: station
        onNotify: banner.show(message)
    }

    Rectangle {
        id: banner
        function show(msg) { bannerLabel.text = msg; opacity = 1; bannerTimer.restart() }
        z: 100
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: Theme.paddingLarge }
        height: bannerLabel.height + 2 * Theme.paddingMedium
        radius: Theme.paddingSmall
        color: Theme.rgba(Theme.highlightDimmerColor, 0.95)
        opacity: 0
        visible: opacity > 0
        Behavior on opacity { FadeAnimation {} }
        Label {
            id: bannerLabel
            anchors.centerIn: parent
            width: parent.width - 2 * Theme.paddingLarge
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.highlightColor
        }
        Timer { id: bannerTimer; interval: 3500; onTriggered: banner.opacity = 0 }
    }
}
