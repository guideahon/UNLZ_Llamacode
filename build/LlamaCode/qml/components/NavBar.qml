import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Rectangle {
    id: root
    width: 200
    color: "#181825"

    property int currentIndex: 0
    signal pageSelected(int index)

    readonly property var pages: [
        { label: "Launch",    icon: "🚀" },
        { label: "Profiles",  icon: "📋" },
        { label: "Models",    icon: "📦" },
        { label: "Binaries",  icon: "⚙" },
    ]

    ColumnLayout {
        anchors { fill: parent; margins: 0 }
        spacing: 0

        // App title
        Rectangle {
            Layout.fillWidth: true
            height: 56
            color: "#11111b"
            Text {
                anchors.centerIn: parent
                text: "🦙 LlamaCode"
                font { pixelSize: 16; bold: true }
                color: "#cdd6f4"
            }
        }

        // Nav buttons
        Repeater {
            model: root.pages
            delegate: ItemDelegate {
                Layout.fillWidth: true
                height: 48
                highlighted: root.currentIndex === index
                background: Rectangle {
                    color: parent.highlighted ? "#313244" : (parent.hovered ? "#1e1e2e" : "transparent")
                    Rectangle {
                        visible: parent.parent.highlighted
                        width: 3; height: parent.height
                        anchors.left: parent.left
                        color: "#89b4fa"
                    }
                }
                contentItem: Row {
                    spacing: 12
                    anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                    Text { text: modelData.icon; font.pixelSize: 18; color: "#cdd6f4" }
                    Text {
                        text: modelData.label
                        font.pixelSize: 14
                        color: root.currentIndex === index ? "#cdd6f4" : "#a6adc8"
                    }
                }
                onClicked: { root.currentIndex = index; root.pageSelected(index) }
            }
        }

        Item { Layout.fillHeight: true }

        // Version
        Text {
            Layout.alignment: Qt.AlignHCenter
            bottomPadding: 12
            text: "v" + App.version()
            font.pixelSize: 11
            color: "#585b70"
        }
    }
}
