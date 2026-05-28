import QtQuick
import QtQuick.Layouts
import LlamaCode 1.0

Rectangle {
    id: root
    height: 56
    color: "#181825"

    property string title: ""
    property string subtitle: ""
    property alias actionLabel: actionBtn.text
    signal actionClicked()

    // Bottom border
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#313244" }

    RowLayout {
        anchors { fill: parent; leftMargin: 24; rightMargin: 16 }

        Column {
            Layout.fillWidth: true
            spacing: 2
            Text {
                text: root.title
                font { pixelSize: 18; bold: true }
                color: "#cdd6f4"
            }
            Text {
                visible: root.subtitle.length > 0
                text: root.subtitle
                font.pixelSize: 12
                color: "#585b70"
            }
        }

        LcButton {
            id: actionBtn
            visible: text.length > 0
            onClicked: root.actionClicked()
        }
    }
}
