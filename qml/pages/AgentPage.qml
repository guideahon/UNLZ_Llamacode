import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Item {
    id: root

    property string selectedLaunchId: ""
    property string resolvedAdapter: ""
    property string resolvedAdapterLabel: ""
    property int currentView: 0   // 0 = Vista Agente, 1 = Vista terminal

    function resolveHarness(launchId) {
        if (!launchId || launchId.length === 0) {
            resolvedAdapter = "none"; resolvedAdapterLabel = ""; return
        }
        const lp = App.profileManager.getLaunchProfile(launchId)
        const harnessId = lp.harnessProfileId ?? ""
        if (harnessId.length > 0) {
            const hp = App.profileManager.getHarness(harnessId)
            resolvedAdapter = hp.adapter ?? "none"
            resolvedAdapterLabel = hp.adapter ?? ""
        } else {
            resolvedAdapter = "none"; resolvedAdapterLabel = ""
        }
    }

    Component.onCompleted: {
        if (App.profileManager.launchProfiles.rowCount() > 0) {
            const idx = App.profileManager.launchProfiles.index(0, 0)
            selectedLaunchId = App.profileManager.launchProfiles.data(idx, 257) ?? ""
            resolveHarness(selectedLaunchId)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Header ──────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 48
            color: Theme.baseBg

            RowLayout {
                anchors { fill: parent; leftMargin: 16; rightMargin: 12 }
                spacing: 10

                Text { text: "🤖"; font.pixelSize: 16 }
                Text {
                    text: (App.langV, App.l("agent.title"))
                    color: Theme.textPrimary
                    font { pixelSize: 15; bold: true }
                }

                ComboBox {
                    id: profileCombo
                    Layout.preferredWidth: 200
                    model: App.profileManager.launchProfiles
                    textRole: "name"
                    valueRole: "profileId"
                    enabled: !App.agentRunning
                    background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                    contentItem: Text {
                        text: profileCombo.displayText.length > 0 ? profileCombo.displayText : "—"
                        color: Theme.textPrimary; font.pixelSize: 12; leftPadding: 8
                        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                    }
                    onCurrentValueChanged: {
                        selectedLaunchId = currentValue ?? ""
                        resolveHarness(selectedLaunchId)
                    }
                }

                Rectangle {
                    visible: resolvedAdapter !== "none" && resolvedAdapterLabel.length > 0
                    height: 22; radius: 4; color: Theme.highlight
                    implicitWidth: adapterLabel.implicitWidth + 16
                    Text {
                        id: adapterLabel
                        anchors.centerIn: parent
                        text: resolvedAdapterLabel
                        color: Theme.accent
                        font { pixelSize: 11; bold: true }
                    }
                }

                Rectangle {
                    width: 8; height: 8; radius: 4
                    color: App.agentRunning ? Theme.successText : Theme.errorText
                }
                Text {
                    text: {
                        const _lang = App.langV
                        if (!App.agentRunning) return App.l("agent.stopped")
                        const title = App.opencodeSessionTitle ?? ""
                        const base  = App.activeAgentAdapter + " · " + App.l("agent.running")
                        return title.length > 0 ? (base + "  —  " + title) : base
                    }
                    color: App.agentRunning ? Theme.successText : Theme.textMuted
                    font.pixelSize: 12
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                // View toggle tabs
                Row {
                    spacing: 0
                    Repeater {
                        model: ["Vista Agente", "Vista terminal"]
                        Rectangle {
                            width: tabLabel.implicitWidth + 20
                            height: 28
                            radius: 4
                            color: root.currentView === index ? Theme.highlight : "transparent"
                            border.color: root.currentView === index ? Theme.accent : Theme.borderColor
                            border.width: 1
                            Text {
                                id: tabLabel
                                anchors.centerIn: parent
                                text: modelData
                                color: root.currentView === index ? Theme.accent : Theme.textMuted
                                font { pixelSize: 11; bold: root.currentView === index }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.currentView = index
                            }
                        }
                    }
                }

                LcButton {
                    text: (App.langV, App.l("agent.clear"))
                    secondary: true
                    visible: !App.agentRunning
                    onClicked: App.clearAgentLog()
                }
                LcButton {
                    text: "Ver log nativo"
                    secondary: true
                    visible: resolvedAdapter.length > 0 && resolvedAdapter !== "none"
                    onClicked: App.openAgentLogDir(resolvedAdapter)
                }
                LcButton {
                    text: {
                        const _lang = App.langV
                        return App.agentRunning ? App.l("agent.stop") : App.l("agent.start")
                    }
                    danger: App.agentRunning
                    enabled: selectedLaunchId.length > 0
                    onClicked: App.agentRunning ? App.stopAgent() : App.startAgent(selectedLaunchId)
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

        Rectangle {
            Layout.fillWidth: true; height: 32
            visible: selectedLaunchId.length > 0
                     && (resolvedAdapter === "none" || resolvedAdapterLabel.length === 0)
                     && !App.agentRunning
            color: Theme.surfaceBg
            Text {
                anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 16 }
                text: (App.langV, App.l("agent.noHarness"))
                color: Theme.textMuted; font.pixelSize: 12
            }
        }

        Rectangle {
            Layout.fillWidth: true; height: 32
            visible: resolvedAdapter !== "none" && resolvedAdapterLabel.length > 0
                     && !(App.harnessCheckV, App.isHarnessInstalled(resolvedAdapter))
                     && !App.agentRunning
            color: Theme.errorBg
            Text {
                anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 16 }
                text: (App.langV, App.l("agent.notInstalled"))
                color: Theme.errorText; font.pixelSize: 12
            }
        }

        // ── Body ─────────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ── Sessions panel ───────────────────────────────────────────────
            Rectangle {
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                visible: App.agentRunning && root.currentView === 0
                color: Theme.surfaceBg
                border.color: Theme.divider
                border.width: 0

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        height: 40
                        color: Theme.baseBg

                        RowLayout {
                            anchors { fill: parent; leftMargin: 10; rightMargin: 8 }
                            spacing: 6
                            Text {
                                text: "Sesiones"
                                color: Theme.textPrimary
                                font { pixelSize: 12; bold: true }
                                Layout.fillWidth: true
                            }
                            LcButton {
                                text: "+"
                                secondary: true
                                implicitWidth: 28
                                implicitHeight: 24
                                onClicked: App.newOpencodeSession()
                            }
                            LcButton {
                                text: "↻"
                                secondary: true
                                implicitWidth: 28
                                implicitHeight: 24
                                onClicked: App.refreshOpencodeSessionList()
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                    ListView {
                        id: sessionsList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: App.agentSessions
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        section.property: "projectName"
                        section.criteria: ViewSection.FullString


                        section.delegate: Rectangle {
                            width: sessionsList.width
                            height: 28
                            color: Theme.baseBg

                            RowLayout {
                                anchors { fill: parent; leftMargin: 8; rightMargin: 6 }
                                spacing: 4
                                Text { text: "📁"; font.pixelSize: 10 }
                                Text {
                                    Layout.fillWidth: true
                                    text: section
                                    color: Theme.textMuted
                                    font { pixelSize: 10; bold: true }
                                    elide: Text.ElideLeft
                                }
                                // "+" per project → new session in this project
                                Rectangle {
                                    width: 20; height: 20; radius: 4
                                    color: addSessionHover.containsMouse ? Theme.highlight : "transparent"
                                    Text {
                                        anchors.centerIn: parent
                                        text: "+"
                                        color: Theme.textMuted
                                        font { pixelSize: 13; bold: true }
                                    }
                                    MouseArea {
                                        id: addSessionHover
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: App.newOpencodeSession()
                                    }
                                }
                            }
                        }

                        delegate: Rectangle {
                            width: sessionsList.width
                            height: 52
                            color: modelData.id === (App.opencodeSessionId ?? "")
                                   ? Theme.highlight : "transparent"

                            Rectangle {
                                width: 3; height: parent.height
                                color: modelData.id === (App.opencodeSessionId ?? "")
                                       ? Theme.accent : "transparent"
                            }

                            ColumnLayout {
                                anchors {
                                    verticalCenter: parent.verticalCenter
                                    left: parent.left; leftMargin: 14
                                    right: parent.right; rightMargin: 8
                                }
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: {
                                        const t = modelData.title ?? ""
                                        return t.length > 0 ? t : "Nueva sesión"
                                    }
                                    color: modelData.id === (App.opencodeSessionId ?? "")
                                           ? Theme.accent : Theme.textPrimary
                                    font { pixelSize: 12; bold: modelData.id === (App.opencodeSessionId ?? "") }
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: {
                                        const ms = modelData.created ?? 0
                                        return ms > 0 ? new Date(ms).toLocaleDateString(Qt.locale(), "d MMM yyyy") : ""
                                    }
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: App.switchOpencodeSession(modelData.id)
                            }
                        }
                    }

                    // ── Nuevo proyecto ────────────────────────────────────────
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 40
                        color: "transparent"
                        RowLayout {
                            anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                            spacing: 6
                            Text { text: "📁"; font.pixelSize: 11 }
                            Text {
                                text: "Nuevo proyecto"
                                color: Theme.textMuted
                                font.pixelSize: 12
                                Layout.fillWidth: true
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true
                            onEntered: parent.color = Theme.highlight
                            onExited:  parent.color = "transparent"
                            onClicked: {
                                const dir = App.pickDirectory("Seleccionar carpeta del proyecto")
                                if (dir.length > 0) App.changeAgentProject(dir)
                            }
                        }
                    }
                }
            }

            Rectangle { width: 1; Layout.fillHeight: true; color: Theme.divider; visible: App.agentRunning && root.currentView === 0 }

            // ── Main content area ────────────────────────────────────────────
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

            // ── Vista Agente (chat) ───────────────────────────────────────────
            Item {
                anchors.fill: parent
                visible: root.currentView === 0

                // Idle / not running
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 16
                    visible: !App.agentRunning && App.agentMessages.length === 0
                    Text { Layout.alignment: Qt.AlignHCenter; text: "🤖"; font.pixelSize: 48 }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: resolvedAdapter !== "none" && resolvedAdapterLabel.length > 0
                            ? resolvedAdapterLabel + " · " + (App.langV, App.l("agent.stopped"))
                            : (App.langV, App.l("agent.stopped"))
                        color: Theme.textMuted; font.pixelSize: 14
                    }
                }

                // Message list
                ListView {
                    id: msgList
                    anchors.fill: parent
                    clip: true
                    spacing: 4
                    topMargin: 12
                    bottomMargin: 12
                    visible: App.agentMessages.length > 0
                    model: App.agentMessages
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Item {
                        id: delegateRoot
                        width: msgList.width
                        height: bubbleRect.height + 8

                        readonly property bool isUser: modelData.role === "user"
                        readonly property string content: modelData.content ?? ""
                        readonly property bool isTyping: modelData.typing ?? false

                        Rectangle {
                            id: bubbleRect
                            anchors {
                                top: parent.top
                                right: delegateRoot.isUser ? parent.right : undefined
                                rightMargin: delegateRoot.isUser ? 16 : undefined
                                left: delegateRoot.isUser ? undefined : parent.left
                                leftMargin: delegateRoot.isUser ? undefined : 16
                            }
                            width: Math.min(delegateRoot.width - 80, delegateRoot.width * 0.78)
                            height: Math.max(msgText.implicitHeight + 22, 44)
                            radius: 10
                            color: delegateRoot.isUser ? Theme.chatUserBubble : Theme.chatAsstBubble
                            border.width: delegateRoot.isUser ? 0 : 1
                            border.color: Theme.borderColor

                            TextEdit {
                                id: msgText
                                anchors { top: parent.top; left: parent.left; right: parent.right; margins: 11 }
                                text: {
                                    if (delegateRoot.isTyping && delegateRoot.content.length === 0)
                                        return "⏳ Procesando..."
                                    if (delegateRoot.isTyping)
                                        return delegateRoot.content + "▌"
                                    return delegateRoot.content
                                }
                                color: {
                                    if (delegateRoot.isTyping && delegateRoot.content.length === 0)
                                        return Theme.textMuted
                                    return delegateRoot.isUser ? Theme.chatUserText : Theme.chatAsstText
                                }
                                font.family: "Segoe UI"
                                font.pixelSize: 13
                                font.italic: delegateRoot.isTyping && delegateRoot.content.length === 0
                                wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                                readOnly: true
                                selectByMouse: true
                            }
                        }

                        Component.onCompleted: Qt.callLater(() => { msgList.positionViewAtEnd() })
                    }

                    onCountChanged: Qt.callLater(() => { msgList.positionViewAtEnd() })
                }

                // Terminal mode placeholder
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 20
                    visible: App.agentRunning && App.agentInTerminal
                    Text { Layout.alignment: Qt.AlignHCenter; text: "🖥️"; font.pixelSize: 48 }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: App.activeAgentAdapter + " · ejecutándose en terminal externa"
                        color: Theme.successText; font { pixelSize: 15; bold: true }
                    }
                }
            }

            // ── Vista terminal (raw log) ──────────────────────────────────────
            Item {
                anchors.fill: parent
                visible: root.currentView === 1

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    visible: !App.agentRunning && App.agentLog.length === 0

                    Item { Layout.fillWidth: true; Layout.fillHeight: true }
                    ColumnLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 16
                        Text { Layout.alignment: Qt.AlignHCenter; text: "🤖"; font.pixelSize: 48 }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: (App.langV, App.l("agent.stopped"))
                            color: Theme.textMuted; font.pixelSize: 14
                        }
                    }
                    Item { Layout.fillWidth: true; Layout.fillHeight: true }
                }

                Rectangle {
                    anchors.fill: parent
                    color: Theme.logBg
                    visible: App.agentLog.length > 0

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            anchors.margins: 8
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                            TextArea {
                                readOnly: true
                                text: App.agentLog
                                color: Theme.textSecondary
                                font { family: "Consolas,monospace"; pixelSize: 12 }
                                wrapMode: TextArea.WrapAnywhere
                                background: null
                                selectByMouse: true
                                onTextChanged: cursorPosition = length
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true; height: 24
                            color: Theme.surfaceBg
                            visible: resolvedAdapter.length > 0 && resolvedAdapter !== "none"
                            Text {
                                anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 12 }
                                text: "native log: " + App.agentNativeLogDir(resolvedAdapter)
                                color: Theme.textMuted
                                font { family: "Consolas,monospace"; pixelSize: 10 }
                                elide: Text.ElideLeft; width: parent.width - 24
                            }
                        }
                    }
                }
            }

            }  // main content Item
        }  // RowLayout

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

        // ── Input bar ────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 52
            color: Theme.baseBg
            visible: App.agentRunning && !App.agentInTerminal

            RowLayout {
                anchors { fill: parent; margins: 8 }
                spacing: 8

                LcTextField {
                    id: agentInput
                    Layout.fillWidth: true
                    placeholderText: (App.langV, App.l("agent.input"))
                    onAccepted: {
                        const t = text.trim()
                        if (t.length === 0) return
                        App.sendToAgent(t); text = ""
                    }
                }
                LcButton {
                    text: (App.langV, App.l("agent.send"))
                    enabled: agentInput.text.trim().length > 0
                    onClicked: {
                        const t = agentInput.text.trim()
                        if (t.length === 0) return
                        App.sendToAgent(t); agentInput.text = ""
                    }
                }
            }
        }
    }
}
