import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Rectangle {
    id: root
    color: "#11111b"
    radius: 8
    border.color: "#313244"
    clip: true

    property string commandLine: ""
    property var warnings: []
    property var errors: []
    property bool isValid: errors.length === 0

    ColumnLayout {
        anchors { fill: parent; margins: 12 }
        spacing: 8

        // Command line
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "$ "
                font { family: "Consolas,monospace"; pixelSize: 13 }
                color: "#a6e3a1"
            }
            Text {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.commandLine.length > 0 ? root.commandLine : "(no profile selected)"
                font { family: "Consolas,monospace"; pixelSize: 13 }
                color: root.commandLine.length > 0 ? "#cdd6f4" : "#585b70"
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 5
                elide: Text.ElideRight
            }
            LcButton {
                visible: root.commandLine.length > 0
                text: "Copy"
                secondary: true
                onClicked: {
                    App.copyToClipboard(root.commandLine)
                    text = "Copied!"
                    copyTimer.start()
                }
                Timer {
                    id: copyTimer; interval: 1500
                    onTriggered: parent.text = "Copy"
                }
            }
        }

        // Warnings
        Repeater {
            model: root.warnings
            delegate: Row {
                spacing: 6
                Text { text: "⚠"; font.pixelSize: 12; color: "#f9e2af" }
                Text { text: modelData; font.pixelSize: 12; color: "#f9e2af"; wrapMode: Text.Wrap }
            }
        }

        // Errors
        Repeater {
            model: root.errors
            delegate: Row {
                spacing: 6
                Text { text: "✗"; font.pixelSize: 12; color: "#f38ba8" }
                Text { text: modelData; font.pixelSize: 12; color: "#f38ba8"; wrapMode: Text.Wrap }
            }
        }
    }
}
