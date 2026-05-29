import QtQuick
import QtQuick.Controls

Button {
    id: root
    property bool danger: false
    property bool secondary: false

    contentItem: Text {
        text: root.text
        font.pixelSize: 13
        color: root.danger ? "#1e1e2e" : (root.secondary ? "#a6adc8" : "#1e1e2e")
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: 6
        color: {
            if (root.danger)     return root.pressed ? "#a6192f" : (root.hovered ? "#e33054" : "#f38ba8")
            if (root.secondary)  return root.pressed ? "#313244" : (root.hovered ? "#45475a" : "#313244")
            return root.pressed ? "#5e81ac" : (root.hovered ? "#74c7ec" : "#89b4fa")
        }
    }

    implicitHeight: 34
    implicitWidth: contentItem.implicitWidth + 24
    padding: 0
    leftPadding: 12
    rightPadding: 12
}
