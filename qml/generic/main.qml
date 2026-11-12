// SheetStation UI for Windows, Linux and Android (Qt Quick Controls 2)
import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12

ApplicationWindow {
    id: win
    visible: true
    width: 480
    height: 760
    title: "SheetStation"

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 4
            Rectangle {
                width: 10; height: 10; radius: 5
                color: station.sheetOk ? "#3cb371" : "#d9534f"
            }
            Label {
                text: "SheetStation"
                font.bold: true
                font.pixelSize: 18
            }
            Label {
                text: station.status
                elide: Text.ElideRight
                opacity: 0.7
                Layout.fillWidth: true
            }
            ToolButton {
                text: "↻"
                font.pixelSize: 20
                onClicked: station.refresh()
            }
        }
    }

    footer: TabBar {
        id: tabs
        currentIndex: station.configured ? 0 : 3
        TabButton { text: qsTr("Devices") }
        TabButton { text: qsTr("My files") }
        TabButton { text: station.activeTransfers > 0 ? qsTr("Transfers (%1)").arg(station.activeTransfers) : qsTr("Transfers") }
        TabButton { text: qsTr("Settings") }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: tabs.currentIndex

        // ---------------------------------------------------------- devices
        Item {
            TextField {
                id: filter
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                placeholderText: qsTr("Filter files...")
            }
            ListView {
                id: peerList
                anchors { left: parent.left; right: parent.right; top: filter.bottom; bottom: parent.bottom; topMargin: 4 }
                clip: true
                model: station.peers
                spacing: 6
                ScrollBar.vertical: ScrollBar {}

                Label {
                    anchors.centerIn: parent
                    width: parent.width - 48
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    visible: peerList.count === 0
                    opacity: 0.6
                    text: station.configured ? qsTr("No other devices yet.\nOpen SheetStation on another device with the same sheet URL.")
                                             : qsTr("Paste your Apps Script web app URL in Settings to get started.")
                }

                delegate: Frame {
                    width: peerList.width - 16
                    x: 8
                    property var peer: modelData
                    property bool expanded: peer.online
                    opacity: peer.online ? 1.0 : 0.55

                    ColumnLayout {
                        width: parent.width
                        RowLayout {
                            Layout.fillWidth: true
                            Rectangle {
                                width: 10; height: 10; radius: 5
                                color: peer.link === "connected" ? "#3c8dbc" : (peer.online ? "#3cb371" : "#999999")
                            }
                            Label {
                                text: peer.name + "  ·  " + peer.platform
                                font.bold: true
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Label {
                                text: peer.link === "connecting" ? qsTr("connecting...")
                                      : peer.link === "connected" ? qsTr("connected")
                                      : peer.online ? qsTr("%1 files").arg(peer.files.length) : qsTr("offline")
                                opacity: 0.7
                            }
                            ToolButton {
                                text: expanded ? "▴" : "▾"
                                onClicked: expanded = !expanded
                            }
                        }
                        Repeater {
                            model: expanded ? peer.files : []
                            delegate: RowLayout {
                                Layout.fillWidth: true
                                visible: filter.text.length === 0 || modelData.name.toLowerCase().indexOf(filter.text.toLowerCase()) >= 0
                                Label {
                                    text: modelData.name
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: station.formatSize(modelData.size)
                                    opacity: 0.6
                                }
                                Button {
                                    text: qsTr("Get")
                                    enabled: peer.online
                                    onClicked: station.download(peer.id, modelData.name)
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---------------------------------------------------------- my files
        ColumnLayout {
            spacing: 4
            Label {
                Layout.fillWidth: true
                Layout.margins: 12
                wrapMode: Text.WordWrap
                text: qsTr("Files in your shared folder are listed for other devices:\n%1").arg(station.shareDir)
            }
            Button {
                Layout.leftMargin: 12
                text: qsTr("Open shared folder")
                onClicked: station.openPath(station.shareDir)
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: station.myFiles
                delegate: ItemDelegate {
                    width: ListView.view.width
                    text: modelData.name + "   (" + station.formatSize(modelData.size) + ")"
                }
            }
        }

        // ---------------------------------------------------------- transfers
        ColumnLayout {
            RowLayout {
                Layout.margins: 8
                Button { text: qsTr("Clear finished"); onClicked: station.clearFinished() }
                Button { text: qsTr("Open downloads"); onClicked: station.openPath(station.downloadDir) }
            }
            ListView {
                id: transferList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: station.transfers
                spacing: 4
                Label {
                    anchors.centerIn: parent
                    visible: transferList.count === 0
                    opacity: 0.6
                    text: qsTr("No transfers yet")
                }
                delegate: Frame {
                    width: transferList.width - 16
                    x: 8
                    property var t: modelData
                    ColumnLayout {
                        width: parent.width
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: t.direction === "down" ? "⬇" : "⬆" }
                            Label { text: t.name; elide: Text.ElideMiddle; Layout.fillWidth: true; font.bold: true }
                            Label {
                                text: t.state === "failed" ? qsTr("failed") : t.state === "done" ? qsTr("done")
                                      : t.state === "waiting" ? qsTr("waiting") : Math.round(t.progress * 100) + "%"
                                color: t.state === "failed" ? "#d9534f" : palette.text
                            }
                        }
                        ProgressBar {
                            Layout.fillWidth: true
                            value: t.progress
                            visible: t.state === "running" || t.state === "waiting"
                        }
                        Label {
                            Layout.fillWidth: true
                            opacity: 0.7
                            wrapMode: Text.WordWrap
                            text: (t.direction === "down" ? qsTr("from %1") : qsTr("to %1")).arg(t.peerName) + "  ·  "
                                  + station.formatSize(t.bytes) + " / " + station.formatSize(t.total)
                                  + (t.error ? "  ·  " + t.error : "")
                        }
                        Button {
                            visible: t.state === "done" && t.direction === "down" && !!t.path
                            text: qsTr("Open")
                            onClicked: station.openPath(t.path)
                        }
                    }
                }
            }
        }

        // ---------------------------------------------------------- settings
        Flickable {
            contentHeight: settingsCol.height + 24
            clip: true
            ColumnLayout {
                id: settingsCol
                x: 12; y: 12
                width: parent.width - 24
                spacing: 6

                Label { text: qsTr("Apps Script web app URL"); font.bold: true }
                TextField {
                    id: urlField
                    Layout.fillWidth: true
                    text: station.scriptUrl
                    placeholderText: "https://script.google.com/macros/s/.../exec"
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                    selectByMouse: true
                }
                Label { text: qsTr("Device name"); font.bold: true }
                TextField { id: nameField; Layout.fillWidth: true; text: station.deviceName; selectByMouse: true }
                Label { text: qsTr("Shared folder"); font.bold: true }
                TextField { id: shareField; Layout.fillWidth: true; text: station.shareDir; selectByMouse: true }
                Label { text: qsTr("Download folder"); font.bold: true }
                TextField { id: dlField; Layout.fillWidth: true; text: station.downloadDir; selectByMouse: true }
                Label { text: qsTr("STUN servers (optional, comma separated)"); font.bold: true }
                TextField {
                    id: stunField
                    Layout.fillWidth: true
                    text: station.stunServers
                    placeholderText: qsTr("empty = LAN / IPv6 only")
                    selectByMouse: true
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    font.pixelSize: 12
                    text: qsTr("Without a STUN server, devices connect directly when they share a network or both have IPv6. "
                             + "Adding one (e.g. stun.l.google.com:19302) lets most home networks connect, at the cost of using an outside server.")
                }
                Button {
                    text: qsTr("Save")
                    highlighted: true
                    onClicked: {
                        station.deviceName = nameField.text
                        station.shareDir = shareField.text
                        station.downloadDir = dlField.text
                        station.stunServers = stunField.text
                        station.scriptUrl = urlField.text
                        if (station.configured) tabs.currentIndex = 0
                    }
                }
                Label {
                    Layout.topMargin: 16
                    opacity: 0.5
                    font.pixelSize: 11
                    text: qsTr("Device id: %1 (%2)").arg(station.deviceId).arg(station.platform)
                }
            }
        }
    }

    // toast
    Rectangle {
        id: toast
        property alias text: toastLabel.text
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        width: Math.min(parent.width - 32, toastLabel.implicitWidth + 32)
        height: toastLabel.implicitHeight + 20
        radius: 8
        color: "#333333"
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Label {
            id: toastLabel
            anchors.centerIn: parent
            width: parent.width - 32
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            color: "white"
        }
        Timer { id: toastTimer; interval: 3500; onTriggered: toast.opacity = 0 }
    }

    Connections {
        target: station
        // old-style handler syntax so it works on every Qt version
        onNotify: {
            toast.text = message
            toast.opacity = 0.92
            toastTimer.restart()
        }
    }
}
