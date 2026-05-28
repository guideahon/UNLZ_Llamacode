import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

ApplicationWindow {
    id: window
    title: "LlamaCode"
    width: 1200
    height: 760
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    color: "#0b0b10"
    flags: Qt.Window | Qt.FramelessWindowHint

    property color frameBorderColor: active ? "#3f4360" : "#2a2d41"
    property color frameBgColor: "#1e1e2e"
    property color titleBarColor: "#11111b"
    property int resizeHandleSize: 8

    function startResize(edges) {
        if (window.visibility === Window.Maximized)
            return
        window.startSystemResize(edges)
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 0
        radius: 0
        color: window.frameBgColor
        border.width: 0
        clip: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                id: titleBar
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                color: window.titleBarColor

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 0
                    spacing: 0

                    Label {
                        text: "LlamaCode"
                        color: "#cdd6f4"
                        font.pixelSize: 13
                        font.bold: true
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Item { Layout.fillWidth: true }

                    ToolButton {
                        text: "\uE921"
                        flat: true
                        onClicked: window.showMinimized()
                        contentItem: Text {
                            text: parent.text
                            color: "#cdd6f4"
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 10
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle { color: parent.hovered ? "#2a2d41" : "transparent" }
                        Layout.preferredWidth: 46
                        Layout.fillHeight: true
                    }

                    ToolButton {
                        text: window.visibility === Window.Maximized ? "\uE923" : "\uE922"
                        flat: true
                        onClicked: window.visibility === Window.Maximized ? window.showNormal() : window.showMaximized()
                        contentItem: Text {
                            text: parent.text
                            color: "#cdd6f4"
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle { color: parent.hovered ? "#2a2d41" : "transparent" }
                        Layout.preferredWidth: 46
                        Layout.fillHeight: true
                    }

                    ToolButton {
                        text: "\uE8BB"
                        flat: true
                        onClicked: window.close()
                        contentItem: Text {
                            text: parent.text
                            color: "#f5e0dc"
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 10
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle { color: parent.hovered ? "#f38ba8" : "transparent" }
                        Layout.preferredWidth: 46
                        Layout.fillHeight: true
                    }
                }

                TapHandler {
                    onDoubleTapped: window.visibility === Window.Maximized ? window.showNormal() : window.showMaximized()
                }
                DragHandler {
                    target: null
                    onActiveChanged: if (active) window.startSystemMove()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                NavBar {
                    Layout.fillHeight: true
                    currentIndex: stack.currentIndex
                    onPageSelected: function(idx) { stack.currentIndex = idx }
                }

                Rectangle { width: 1; Layout.fillHeight: true; color: "#313244" }

                StackLayout {
                    id: stack
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: 0

                    LaunchPage      {}
                    ProfilesPage    {}
                    ModelRootsPage  { id: modelRootsPage }
                    BinariesPage    { id: binariesPage }
                }
            }
        }
    }

    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: window.resizeHandleSize
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        onPressed: window.startResize(Qt.LeftEdge)
    }
    MouseArea {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: window.resizeHandleSize
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        onPressed: window.startResize(Qt.RightEdge)
    }
    MouseArea {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: window.resizeHandleSize
        hoverEnabled: true
        cursorShape: Qt.SizeVerCursor
        onPressed: window.startResize(Qt.TopEdge)
    }
    MouseArea {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: window.resizeHandleSize
        hoverEnabled: true
        cursorShape: Qt.SizeVerCursor
        onPressed: window.startResize(Qt.BottomEdge)
    }
    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        width: window.resizeHandleSize
        height: window.resizeHandleSize
        hoverEnabled: true
        cursorShape: Qt.SizeFDiagCursor
        onPressed: window.startResize(Qt.TopEdge | Qt.LeftEdge)
    }
    MouseArea {
        anchors.right: parent.right
        anchors.top: parent.top
        width: window.resizeHandleSize
        height: window.resizeHandleSize
        hoverEnabled: true
        cursorShape: Qt.SizeBDiagCursor
        onPressed: window.startResize(Qt.TopEdge | Qt.RightEdge)
    }
    MouseArea {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: window.resizeHandleSize
        height: window.resizeHandleSize
        hoverEnabled: true
        cursorShape: Qt.SizeBDiagCursor
        onPressed: window.startResize(Qt.BottomEdge | Qt.LeftEdge)
    }
    MouseArea {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: window.resizeHandleSize
        height: window.resizeHandleSize
        hoverEnabled: true
        cursorShape: Qt.SizeFDiagCursor
        onPressed: window.startResize(Qt.BottomEdge | Qt.RightEdge)
    }

    // Global error toast
    Popup {
        id: errorToast
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 380
        height: 60
        modal: false

        property string message: ""
        function show(msg) { message = msg; open(); closeTimer.start() }

        background: Rectangle {
            color: "#3a1a1a"; radius: 8
            border.color: "#f38ba8"; border.width: 1
        }

        Text {
            anchors.centerIn: parent
            text: errorToast.message
            color: "#f38ba8"; font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            width: parent.width - 24
        }

        Timer { id: closeTimer; interval: 4000; onTriggered: errorToast.close() }
    }

    Popup {
        id: setupPopup
        parent: Overlay.overlay
        modal: true
        closePolicy: Popup.NoAutoClose
        width: 620
        height: 300
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        background: Rectangle {
            color: "#1b1d31"
            radius: 12
            border.width: 1
            border.color: "#3a3f5c"
        }

        contentItem: ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12

            Text {
                text: "Completar Setup Inicial"
                color: "#cdd6f4"
                font.pixelSize: 20
                font.bold: true
            }
            Text {
                text: "No hay binarios ni modelos registrados. Necesitás instalar/localizar un llama-server y descargar al menos un modelo GGUF."
                color: "#a6adc8"
                wrapMode: Text.Wrap
                font.pixelSize: 13
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#313244" }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                LcButton {
                    text: "Localizar binario"
                    secondary: true
                    onClicked: {
                        stack.currentIndex = 3
                        binariesPage.openAddDialog()
                    }
                }
                LcButton {
                    text: App.installingOfficialBinary ? "Instalando..." : "Instalar binario"
                    enabled: !App.installingOfficialBinary
                    onClicked: App.installOfficialBinary()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                LcButton {
                    text: "Descargar modelo (GGUF)"
                    secondary: true
                    onClicked: Qt.openUrlExternally("https://huggingface.co/models?library=gguf")
                }
                LcButton {
                    text: "Ir a Model Roots"
                    secondary: true
                    onClicked: stack.currentIndex = 2
                }
            }

            Item { Layout.fillHeight: true }
            Text {
                visible: App.needsSetup
                text: "El popup se cierra automáticamente cuando exista al menos 1 binario o 1 modelo."
                color: "#585b70"
                font.pixelSize: 12
            }
        }
    }

    Component.onCompleted: {
        if (App.needsSetup) setupPopup.open()
    }

    Connections {
        target: App
        function onServerError(message) { errorToast.show(message) }
        function onSetupStateChanged() {
            if (App.needsSetup) setupPopup.open()
            else setupPopup.close()
        }
        function onOfficialBinaryInstallFinished(success, message, binaryPath) {
            errorToast.show(message)
            if (success && binaryPath.length > 0) {
                stack.currentIndex = 3
            }
        }
    }
    Connections {
        target: App.binaryRegistry
        function onCapabilitiesDetected(id, success, error) {
            if (!success) errorToast.show("Capability detection failed: " + error)
        }
    }
    Connections {
        target: App.rootRegistry
        function onScanFinished(rootId, count) {
            // Brief notification handled via subtitle update in page
        }
    }
}
