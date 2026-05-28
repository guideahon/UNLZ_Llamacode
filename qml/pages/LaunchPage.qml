import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Item {
    id: root
    property bool logVisible: false
    property real logHeight: 220
    property real minLogHeight: 120

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: "Launch"
            subtitle: App.serverRunning ? "Server running" : "Server stopped"
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 20
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                spacing: 16

                Text { text: "Launch Profile"; color: "#585b70"; font.pixelSize: 12 }

                ComboBox {
                    id: launchCombo
                    Layout.fillWidth: true
                    model: App.profileManager.launchProfiles
                    textRole: "name"
                    valueRole: "profileId"
                    background: Rectangle { color: "#11111b"; radius: 6; border.color: "#313244" }
                    contentItem: Text {
                        text: launchCombo.displayText.length > 0 ? launchCombo.displayText : "— select —"
                        color: "#cdd6f4"; font.pixelSize: 13; leftPadding: 10
                        verticalAlignment: Text.AlignVCenter
                    }
                    onCurrentValueChanged: {
                        if (currentValue) App.computeEffectiveProfile(currentValue)
                    }
                }

                // Effective profile summary
                Rectangle {
                    Layout.fillWidth: true
                    height: summaryCol.implicitHeight + 16
                    color: "#11111b"
                    radius: 8
                    border.color: "#313244"
                    visible: launchCombo.currentValue !== undefined

                    Column {
                        id: summaryCol
                        anchors { fill: parent; margins: 12 }
                        spacing: 6

                        Repeater {
                            model: [
                                ["Binary",  App.effectiveProfile.binaryPath?.split(/[/\\]/).pop() ?? "—"],
                                ["Valid",   App.effectiveProfile.isValid ? "Yes" : "No"],
                            ]
                            delegate: Row {
                                spacing: 8
                                Text { text: modelData[0] + ":"; color: "#585b70"; font.pixelSize: 12; width: 60 }
                                Text {
                                    text: modelData[1]
                                    color: modelData[0] === "Valid"
                                           ? (modelData[1] === "Yes" ? "#a6e3a1" : "#f38ba8")
                                           : "#a6adc8"
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }
                }

                Text { text: "Command Preview"; color: "#585b70"; font.pixelSize: 12 }

                CommandPreview {
                    Layout.fillWidth: true
                    height: 120
                    commandLine: App.effectiveProfile.commandLine ?? ""
                    warnings: App.effectiveProfile.warnings ?? []
                    errors: App.effectiveProfile.blockingErrors ?? []
                }

                // Start/Stop
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    LcButton {
                        text: App.serverRunning ? "Stop Server" : "Start Server"
                        danger: App.serverRunning
                        Layout.fillWidth: true
                        enabled: launchCombo.currentValue !== undefined || App.serverRunning
                        onClicked: {
                            if (App.serverRunning) App.stopServer()
                            else App.startServer(launchCombo.currentValue ?? "")
                        }
                    }

                    LcButton {
                        text: "Preview"
                        secondary: true
                        enabled: launchCombo.currentValue !== undefined
                        onClicked: App.computeEffectiveProfile(launchCombo.currentValue)
                    }
                }

                // Status indicator
                Rectangle {
                    Layout.fillWidth: true
                    height: 36
                    radius: 6
                    color: App.serverRunning ? "#1a3a1a" : "#3a1a1a"
                    border.color: App.serverRunning ? "#a6e3a1" : "#f38ba8"

                    Row {
                        anchors.centerIn: parent
                        spacing: 8
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            anchors.verticalCenter: parent.verticalCenter
                            color: App.serverRunning ? "#a6e3a1" : "#f38ba8"
                            SequentialAnimation on opacity {
                                running: App.serverRunning
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.3; duration: 800 }
                                NumberAnimation { to: 1.0; duration: 800 }
                            }
                        }
                        Text {
                            text: App.serverRunning ? "Server running" : "Server stopped"
                            color: App.serverRunning ? "#a6e3a1" : "#f38ba8"
                            font.pixelSize: 13
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 44
            color: "#181825"
            border.color: "#313244"
            border.width: 1

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 16
                spacing: 8

                LcButton {
                    text: root.logVisible ? "Ocultar log" : "Ver log"
                    secondary: true
                    onClicked: root.logVisible = !root.logVisible
                }
                LcButton {
                    text: "Clear"
                    secondary: true
                    onClicked: App.clearLog()
                    enabled: root.logVisible
                }
            }
        }

        Rectangle {
            visible: root.logVisible
            Layout.fillWidth: true
            Layout.preferredHeight: root.logVisible ? root.logHeight : 0
            Layout.maximumHeight: root.height * 0.75
            color: "#0f1020"
            border.color: "#313244"
            border.width: 1
            clip: true

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: 120; easing.type: Easing.OutQuad }
            }

            Rectangle {
                id: splitter
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 8
                color: dragArea.containsMouse ? "#3b3f63" : "#2a2d41"

                MouseArea {
                    id: dragArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.SizeVerCursor
                    property real startY: 0
                    property real startHeight: 0

                    onPressed: function(mouse) {
                        startY = mouse.y
                        startHeight = root.logHeight
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        const delta = mouse.y - startY
                        const next = startHeight - delta
                        const maxH = root.height * 0.75
                        root.logHeight = Math.max(root.minLogHeight, Math.min(maxH, next))
                    }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 10
                anchors.margins: 8
                spacing: 6

                Text {
                    text: "Server Log"
                    color: "#585b70"
                    font.pixelSize: 12
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#11111b"
                    radius: 8
                    border.color: "#313244"
                    clip: true

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 8
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                        TextArea {
                            readOnly: true
                            text: App.serverLog
                            color: "#a6adc8"
                            font { family: "Consolas,monospace"; pixelSize: 12 }
                            wrapMode: TextArea.WrapAnywhere
                            background: null
                            onTextChanged: cursorPosition = text.length
                        }
                    }
                }
            }
        }
    }
}
