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
    color: Theme.windowBg
    flags: Qt.Window | Qt.FramelessWindowHint

    property color frameBorderColor: active ? Theme.frameBorderActive : Theme.frameBorderInact
    property color frameBgColor: Theme.baseBg
    property color titleBarColor: Theme.titleBg
    property int resizeHandleSize: 8
    property bool restoringWindowState: true

    function saveWindowState() {
        if (restoringWindowState)
            return
        if (window.visibility === Window.Maximized) {
            App.writeSetting("window/maximized", true)
            return
        }
        if (window.visibility !== Window.Windowed)
            return
        App.writeSetting("window/maximized", false)
        App.writeSetting("window/x", x)
        App.writeSetting("window/y", y)
        App.writeSetting("window/width", width)
        App.writeSetting("window/height", height)
    }

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
                        color: Theme.textPrimary
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
                            color: Theme.textPrimary
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 10
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle { color: parent.hovered ? Theme.frameBorderInact : "transparent" }
                        Layout.preferredWidth: 46
                        Layout.fillHeight: true
                    }

                    ToolButton {
                        text: window.visibility === Window.Maximized ? "\uE923" : "\uE922"
                        flat: true
                        onClicked: window.visibility === Window.Maximized ? window.showNormal() : window.showMaximized()
                        contentItem: Text {
                            text: parent.text
                            color: Theme.textPrimary
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle { color: parent.hovered ? Theme.frameBorderInact : "transparent" }
                        Layout.preferredWidth: 46
                        Layout.fillHeight: true
                    }

                    ToolButton {
                        text: "\uE8BB"
                        flat: true
                        onClicked: window.close()
                        contentItem: Text {
                            text: parent.text
                            color: Theme.errorText
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 10
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle { color: parent.hovered ? Theme.closeHoverBg : "transparent" }
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

                Rectangle { width: 1; Layout.fillHeight: true; color: Theme.divider }

                StackLayout {
                    id: stack
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: 0

                    LaunchPage      {}
                    ProfilesPage    {}
                    ModelRootsPage  { id: modelRootsPage }
                    BinariesPage    { id: binariesPage }
                    ChatPage        {}
                    AgentPage       {}
                    BenchmarkPage   {}
                    SettingsPage    {}
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
            color: Theme.errorBg; radius: 8
            border.color: Theme.errorBorder; border.width: 1
        }

        Text {
            anchors.centerIn: parent
            text: errorToast.message
            color: Theme.errorText; font.pixelSize: 13
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
        clip: true
        closePolicy: Popup.NoAutoClose
        width: 620
        height: 300
        padding: 18
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        background: Rectangle {
            color: Theme.popupBg
            radius: 12
            border.width: 1
            border.color: Theme.popupBorderColor
        }

        contentItem: ColumnLayout {
            width: setupPopup.availableWidth
            height: setupPopup.availableHeight
            spacing: 12

            Text {
                text: (App.langV, App.l("setup.title"))
                color: Theme.textPrimary
                font.pixelSize: 20
                font.bold: true
            }
            Text {
                text: (App.langV, App.l("setup.description"))
                color: Theme.textSecondary
                Layout.fillWidth: true
                Layout.preferredWidth: setupPopup.availableWidth
                Layout.maximumWidth: setupPopup.availableWidth
                clip: true
                wrapMode: Text.WordWrap
                font.pixelSize: 13
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                LcButton {
                    text: (App.langV, App.l("setup.locateBinary"))
                    secondary: true
                    onClicked: { stack.currentIndex = 3; binariesPage.openAddDialog() }
                }
                LcButton {
                    text: {
                        const _lang = App.langV
                        return App.installingOfficialBinary ? App.l("setup.installing") : App.l("setup.installBinary")
                    }
                    enabled: !App.installingOfficialBinary
                    onClicked: App.installOfficialBinary()
                }
                LcButton {
                    visible: App.installingOfficialBinary
                    text: (App.langV, App.l("setup.cancel"))
                    secondary: true
                    onClicked: App.cancelOfficialBinaryInstall()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: App.installingOfficialBinary || App.officialBinaryInstallStatus.length > 0
                spacing: 8
                BusyIndicator {
                    running: App.installingOfficialBinary
                    visible: App.installingOfficialBinary
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                }
                Text {
                    Layout.fillWidth: true
                    text: App.officialBinaryInstallStatus
                    color: App.installingOfficialBinary ? Theme.accent : Theme.errorText
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                LcButton {
                    visible: !App.installingOfficialBinary && App.officialBinaryInstallStatus.length > 0
                    text: (App.langV, App.l("setup.viewLog"))
                    secondary: true
                    onClicked: installLogPopup.open()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                LcButton {
                    text: (App.langV, App.l("setup.downloadModel"))
                    secondary: true
                    onClicked: Qt.openUrlExternally("https://huggingface.co/models?library=gguf")
                }
                LcButton {
                    text: (App.langV, App.l("setup.goToModels"))
                    secondary: true
                    onClicked: stack.currentIndex = 2
                }
            }

            Item { Layout.fillHeight: true }
            Text {
                visible: App.needsSetup
                text: (App.langV, App.l("setup.tip"))
                color: Theme.textMuted
                font.pixelSize: 12
            }
        }
    }

    Popup {
        id: installLogPopup
        parent: Overlay.overlay
        modal: true
        clip: true
        width: Math.min(window.width - 80, 900)
        height: Math.min(window.height - 80, 520)
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: 14

        background: Rectangle {
            color: Theme.popupBg
            radius: 10
            border.width: 1
            border.color: Theme.popupBorderColor
        }

        contentItem: ColumnLayout {
            width: installLogPopup.availableWidth
            height: installLogPopup.availableHeight
            spacing: 10

            Text {
                text: (App.langV, App.l("setup.installLog"))
                color: Theme.textPrimary
                font.pixelSize: 16
                font.bold: true
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    readOnly: true
                    wrapMode: TextArea.WrapAnywhere
                    text: App.officialBinaryInstallLog
                    color: Theme.textPrimary
                    font.family: "Consolas"
                    font.pixelSize: 12
                    background: Rectangle { color: Theme.inputBg; radius: 6 }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                LcButton {
                    text: (App.langV, App.l("setup.copyLog"))
                    secondary: true
                    onClicked: App.copyToClipboard(App.officialBinaryInstallLog)
                }
                LcButton { text: (App.langV, App.l("common.close")); onClicked: installLogPopup.close() }
            }
        }
    }

    Component.onCompleted: {
        const savedX = Number(App.readSetting("window/x", 100))
        const savedY = Number(App.readSetting("window/y", 100))
        const savedW = Number(App.readSetting("window/width", 1200))
        const savedH = Number(App.readSetting("window/height", 760))
        const savedMaximized = Boolean(App.readSetting("window/maximized", false))

        const restoredW = Math.max(minimumWidth, savedW)
        const restoredH = Math.max(minimumHeight, savedH)
        width = restoredW
        height = restoredH
        x = savedX
        y = savedY
        if (savedMaximized)
            showMaximized()
        restoringWindowState = false

        if (App.needsSetup) setupPopup.open()
    }

    onClosing: saveWindowState()

    onXChanged: saveWindowState()
    onYChanged: saveWindowState()
    onWidthChanged: saveWindowState()
    onHeightChanged: saveWindowState()
    onVisibilityChanged: saveWindowState()

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
