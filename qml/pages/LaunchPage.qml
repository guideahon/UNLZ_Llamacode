import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Item {
    id: root
    property bool logVisible: false
    property real logHeight: 220
    property real minLogHeight: 120
    property bool _restored: false   // evita pisar la setting durante la carga inicial

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: (App.langV, App.l("launch.title"))
            subtitle: {
                const _lang = App.langV
                return App.serverRunning ? App.l("launch.running") : App.l("launch.stopped")
            }
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

                Text { text: (App.langV, App.l("launch.profile")); color: Theme.textMuted; font.pixelSize: 12 }

                ComboBox {
                    id: launchCombo
                    Layout.fillWidth: true
                    model: App.profileManager.launchProfiles
                    textRole: "name"
                    valueRole: "profileId"
                    background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                    contentItem: Text {
                        text: {
                            const _lang = App.langV
                            return launchCombo.displayText.length > 0 ? launchCombo.displayText : App.l("common.selectPlaceholder")
                        }
                        color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10
                        verticalAlignment: Text.AlignVCenter
                    }
                    onCurrentValueChanged: {
                        if (currentValue) {
                            App.computeEffectiveProfile(currentValue)
                            // Recordar el último perfil usado (no durante la carga inicial).
                            if (root._restored) App.writeSetting("lastLaunchId", currentValue)
                        }
                    }

                    // Al abrir la app, restaurar el último perfil de lanzamiento usado.
                    Component.onCompleted: {
                        const last = App.readSetting("lastLaunchId", "")
                        if (last && last.length > 0) {
                            const i = launchCombo.indexOfValue(last)
                            if (i >= 0) launchCombo.currentIndex = i
                        }
                        root._restored = true
                        if (launchCombo.currentValue) App.computeEffectiveProfile(launchCombo.currentValue)
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: summaryCol.implicitHeight + 16
                    color: Theme.inputBg
                    radius: 8
                    border.color: Theme.borderColor
                    visible: launchCombo.currentValue !== undefined

                    Column {
                        id: summaryCol
                        anchors { fill: parent; margins: 12 }
                        spacing: 6

                        Repeater {
                            model: [
                                [App.l("launch.binary"),  App.effectiveProfile.binaryPath?.split(/[/\\]/).pop() ?? "—"],
                                [App.l("launch.valid"),   App.effectiveProfile.isValid ? App.l("launch.yes") : App.l("launch.no")],
                            ]
                            delegate: Row {
                                spacing: 8
                                Text { text: modelData[0] + ":"; color: Theme.textMuted; font.pixelSize: 12; width: 60 }
                                Text {
                                    text: modelData[1]
                                    color: modelData[0] === App.l("launch.valid")
                                           ? (modelData[1] === App.l("launch.yes") ? Theme.successText : Theme.errorText)
                                           : Theme.textSecondary
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }
                }

                Text { text: (App.langV, App.l("launch.cmdPreview")); color: Theme.textMuted; font.pixelSize: 12 }

                CommandPreview {
                    Layout.fillWidth: true
                    height: 120
                    commandLine: App.effectiveProfile.commandLine ?? ""
                    warnings: App.effectiveProfile.warnings ?? []
                    errors: App.effectiveProfile.blockingErrors ?? []
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    LcButton {
                        text: {
                            const _lang = App.langV
                            if (App.serverStopping) return "Deteniendo..."
                            if (App.serverRunning) return App.l("launch.stopServer")
                            if (launchCombo.count === 0) return "Crear un perfil primero"
                            return App.l("launch.startServer")
                        }
                        danger: App.serverRunning && !App.serverStopping
                        secondary: App.serverStopping
                        Layout.fillWidth: true
                        enabled: !App.serverStopping && launchCombo.count > 0 && (launchCombo.currentValue !== undefined || App.serverRunning)
                        onClicked: {
                            if (App.serverRunning) App.stopServer()
                            else App.startServerAndAgent(launchCombo.currentValue ?? "")
                        }
                    }

                    LcButton {
                        text: (App.langV, App.l("launch.preview"))
                        secondary: true
                        enabled: launchCombo.currentValue !== undefined
                        onClicked: App.computeEffectiveProfile(launchCombo.currentValue)
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 44
            color: Theme.navBg
            border.color: Theme.borderColor
            border.width: 1

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 16
                spacing: 8

                LcButton {
                    text: {
                        const _lang = App.langV
                        return root.logVisible ? App.l("launch.hideLog") : App.l("launch.showLog")
                    }
                    secondary: true
                    onClicked: root.logVisible = !root.logVisible
                }
                LcButton {
                    text: (App.langV, App.l("launch.clear"))
                    secondary: true
                    onClicked: App.clearLog()
                    enabled: root.logVisible
                }
                LcButton {
                    text: (App.langV, App.l("launch.copyLogs"))
                    secondary: true
                    enabled: root.logVisible
                    onClicked: App.copyToClipboard(App.serverLog)
                }
            }
        }

        Rectangle {
            visible: root.logVisible
            Layout.fillWidth: true
            Layout.preferredHeight: root.logVisible ? root.logHeight : 0
            Layout.maximumHeight: root.height * 0.75
            color: Theme.logBg
            border.color: Theme.borderColor
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
                color: dragArea.containsMouse ? Theme.splitterHover : Theme.splitterNormal

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
                    text: (App.langV, App.l("launch.serverLog"))
                    color: Theme.textMuted
                    font.pixelSize: 12
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.inputBg
                    radius: 8
                    border.color: Theme.borderColor
                    clip: true

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 8
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                        TextArea {
                            readOnly: true
                            text: App.serverLog
                            color: Theme.textSecondary
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
