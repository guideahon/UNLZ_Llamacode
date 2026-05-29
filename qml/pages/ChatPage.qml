import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Item {
    id: root

    property bool newProjectDialogOpen: false

    function scrollToBottom() { Qt.callLater(() => { msgList.positionViewAtEnd() }) }

    function projectIdForSection(sectionName) {
        for (let i = 0; i < App.chatSessions.length; i++) {
            if ((App.chatSessions[i].projectName ?? "") === sectionName)
                return App.chatSessions[i].projectId ?? ""
        }
        return ""
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── Sessions panel ───────────────────────────────────────────────────
        Rectangle {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            visible: App.serverRunning
            color: Theme.surfaceBg

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
                            text: "Chats"
                            color: Theme.textPrimary
                            font { pixelSize: 12; bold: true }
                            Layout.fillWidth: true
                        }
                        LcButton {
                            text: "+"
                            secondary: true
                            implicitWidth: 28; implicitHeight: 24
                            onClicked: App.newChatSession()
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                ListView {
                    id: sessionsList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: App.chatSessions
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
                            Rectangle {
                                width: 20; height: 20; radius: 4
                                color: addChatHover.containsMouse ? Theme.highlight : "transparent"
                                Text {
                                    anchors.centerIn: parent
                                    text: "+"
                                    color: Theme.textMuted
                                    font { pixelSize: 13; bold: true }
                                }
                                MouseArea {
                                    id: addChatHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        const pid  = root.projectIdForSection(section)
                                        App.newChatSessionInProject(pid, section)
                                    }
                                }
                            }
                        }
                    }

                    delegate: Rectangle {
                        width: sessionsList.width
                        height: 52
                        color: modelData.id === (App.chatSessionId ?? "")
                               ? Theme.highlight : "transparent"

                        Rectangle {
                            width: 3; height: parent.height
                            color: modelData.id === (App.chatSessionId ?? "")
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
                                    return t.length > 0 ? t : "Nuevo chat"
                                }
                                color: modelData.id === (App.chatSessionId ?? "")
                                       ? Theme.accent : Theme.textPrimary
                                font { pixelSize: 12; bold: modelData.id === (App.chatSessionId ?? "") }
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: {
                                    const ms = modelData.updated ?? modelData.created ?? 0
                                    return ms > 0 ? new Date(ms).toLocaleDateString(Qt.locale(), "d MMM yyyy") : ""
                                }
                                color: Theme.textMuted
                                font.pixelSize: 10
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: App.switchChatSession(modelData.id)
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
                            onClicked: newProjectPopup.open()
                        }
                    }
                }
            }

            // ── Nuevo proyecto popup ──────────────────────────────────────────
            Dialog {
                id: newProjectPopup
                modal: true
                parent: Overlay.overlay
                x: Math.round((parent.width - width) / 2)
                y: Math.round((parent.height - height) / 2)
                width: 420
                height: 200
                closePolicy: Popup.CloseOnEscape

                background: Rectangle {
                    color: Theme.popupBg; radius: 12
                    border.color: Theme.popupBorderColor; border.width: 1
                }
                Overlay.modal: Rectangle { color: Theme.overlayColor }

                header: Rectangle {
                    color: Theme.popupHeaderBg; height: 50; radius: 12
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
                    Text {
                        anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
                        text: "Nuevo proyecto"
                        font { pixelSize: 14; bold: true }
                        color: Theme.textPrimary
                    }
                }

                footer: Rectangle {
                    color: Theme.popupHeaderBg; height: 50; radius: 12
                    Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
                    Rectangle { anchors.top: parent.top; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
                    Row {
                        anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                        spacing: 10
                        LcButton {
                            text: "Cancelar"; secondary: true
                            onClicked: { newProjectPopup.close(); newProjNameField.text = "" }
                        }
                        LcButton {
                            text: "Crear"
                            enabled: newProjNameField.text.trim().length > 0
                            onClicked: {
                                const name = newProjNameField.text.trim()
                                const pid  = "custom-" + name.toLowerCase().replace(/\s+/g, "-")
                                App.newChatSessionInProject(pid, name)
                                newProjectPopup.close()
                                newProjNameField.text = ""
                            }
                        }
                    }
                }

                contentItem: Item {
                    width: 380; height: 46
                    LcTextField {
                        id: newProjNameField
                        anchors.fill: parent
                        placeholderText: "Nombre del proyecto"
                        Keys.onReturnPressed: {
                            if (text.trim().length === 0) return
                            const name = text.trim()
                            const pid  = "custom-" + name.toLowerCase().replace(/\s+/g, "-")
                            App.newChatSessionInProject(pid, name)
                            newProjectPopup.close(); text = ""
                        }
                    }
                }

                onOpened: newProjNameField.forceActiveFocus()
            }
        }

        Rectangle { width: 1; Layout.fillHeight: true; color: Theme.divider; visible: App.serverRunning }

        // ── Main chat area ───────────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Header
            Rectangle {
                Layout.fillWidth: true
                height: 48
                color: Theme.baseBg

                RowLayout {
                    anchors { fill: parent; leftMargin: 16; rightMargin: 12 }
                    spacing: 10
                    Text { text: "Chat"; color: Theme.textPrimary; font.pixelSize: 15; font.bold: true }
                    Rectangle { width: 8; height: 8; radius: 4; color: App.serverRunning ? Theme.successText : Theme.errorText }
                    Text {
                        text: {
                            const _lang = App.langV
                            if (!App.serverRunning) return App.l("chat.serverStopped")
                            const title = App.chatSessionTitle ?? ""
                            return title.length > 0 ? title : App.activeLaunchId
                        }
                        color: App.serverRunning ? Theme.textSecondary : Theme.errorText
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    LcButton {
                        text: (App.langV, App.l("chat.clear"))
                        secondary: true
                        visible: App.chatMessages.length > 0 && !App.chatGenerating
                        onClicked: App.newChatSession()
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

            // Messages
            ListView {
                id: msgList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                topMargin: 12
                bottomMargin: 12
                model: App.chatMessages
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                Text {
                    anchors.centerIn: parent
                    visible: App.chatMessages.length === 0
                    text: {
                        const _lang = App.langV
                        return App.serverRunning ? App.l("chat.startMessage") : App.l("chat.startServer")
                    }
                    color: Theme.textMuted
                    font.pixelSize: 14
                }

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
                onModelChanged: Qt.callLater(() => { msgList.positionViewAtEnd() })
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

            // Input bar
            Rectangle {
                Layout.fillWidth: true
                color: Theme.baseBg
                height: 60

                RowLayout {
                    anchors { fill: parent; leftMargin: 12; rightMargin: 12; topMargin: 10; bottomMargin: 10 }
                    spacing: 8

                    TextField {
                        id: inputField
                        Layout.fillWidth: true
                        placeholderText: {
                            const _lang = App.langV
                            return App.chatGenerating ? App.l("chat.generating") : App.l("chat.placeholder")
                        }
                        enabled: App.serverRunning && !App.chatGenerating
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textMuted
                        font.pixelSize: 13
                        leftPadding: 12; rightPadding: 12
                        verticalAlignment: TextInput.AlignVCenter
                        background: Rectangle {
                            color: Theme.inputBg; radius: 8
                            border.width: inputField.activeFocus ? 1 : 0
                            border.color: Theme.inputBorderFocus
                        }
                        Keys.onReturnPressed: (event) => {
                            if (!(event.modifiers & Qt.ShiftModifier)) {
                                const t = inputField.text.trim()
                                if (t.length > 0) { App.sendChatMessage(t); inputField.text = "" }
                            }
                        }
                    }

                    LcButton {
                        text: {
                            const _lang = App.langV
                            return App.chatGenerating ? App.l("chat.stop") : App.l("chat.send")
                        }
                        danger: App.chatGenerating
                        enabled: App.serverRunning && (App.chatGenerating || inputField.text.trim().length > 0)
                        onClicked: {
                            if (App.chatGenerating) { App.stopChatGeneration(); return }
                            const t = inputField.text.trim()
                            if (t.length > 0) { App.sendChatMessage(t); inputField.text = "" }
                        }
                    }
                }
            }
        }
    }
}
