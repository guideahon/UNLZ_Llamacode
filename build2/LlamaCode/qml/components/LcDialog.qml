import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Dialog {
    id: root
    modal: true
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    leftPadding: 18
    rightPadding: 18
    topPadding: 14
    bottomPadding: 14
    clip: true

    implicitWidth: Math.max(420, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: implicitHeaderHeight + implicitContentHeight + implicitFooterHeight + topPadding + bottomPadding
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: "#1b1d31"
        radius: 12
        border.color: "#3a3f5c"
        border.width: 1
    }

    Overlay.modal: Rectangle {
        color: "#90090b14"
    }

    header: Rectangle {
        color: "#14162a"
        height: 56
        radius: 12
        // mask bottom corners
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: "#14162a" }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#333754" }
        Text {
            anchors { left: parent.left; leftMargin: 22; verticalCenter: parent.verticalCenter }
            text: root.title
            font { pixelSize: 28/2; bold: true }
            color: "#cdd6f4"
        }
    }

    footer: Rectangle {
        color: "#14162a"
        height: 56
        radius: 12
        // mask top corners
        Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: "#14162a" }
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: "#333754" }
        Row {
            anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
            spacing: 10
            LcButton { text: "Cancel"; secondary: true; onClicked: root.reject() }
            LcButton { text: "OK"; onClicked: root.accept() }
        }
    }
}
