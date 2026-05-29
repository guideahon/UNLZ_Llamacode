import QtQuick
import QtQuick.Controls

TextField {
    id: root
    color: "#cdd6f4"
    placeholderTextColor: "#585b70"
    font.pixelSize: 13
    leftPadding: 10
    rightPadding: 10
    verticalAlignment: TextInput.AlignVCenter
    implicitHeight: 34

    background: Rectangle {
        radius: 6
        color: "#11111b"
        border.color: root.activeFocus ? "#89b4fa" : "#313244"
        border.width: root.activeFocus ? 2 : 1
    }
}
