import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Item {
    id: root

    property bool newProjectDialogOpen: false
    property var thinkExpanded: ({})
    property bool thinkingEnabled: (App.readSetting("chat/thinkingEnabled", true) ?? true)
    property var chatAttachments: []

    function fileName(p) { return p.split(/[\\/]/).pop() }

    function sendNow() {
        if (App.chatGenerating) { App.stopChatGeneration(); return }
        const t = inputField.text.trim()
        if (t.length === 0 && root.chatAttachments.length === 0) return
        if (root.chatAttachments.length > 0)
            App.sendChatMessageWithAttachments(t, root.chatAttachments)
        else
            App.sendChatMessage(t)
        inputField.text = ""
        root.chatAttachments = []
    }

    function scrollToBottom() { Qt.callLater(() => { msgList.positionViewAtEnd() }) }

    function projectIdForSection(sectionName) {
        for (let i = 0; i < App.chatSessions.length; i++) {
            if ((App.chatSessions[i].projectName ?? "") === sectionName)
                return App.chatSessions[i].projectId ?? ""
        }
        return ""
    }

    function extractThinkContent(text) {
        if (!text || text.length === 0) return ""
        let out = ""
        const re = /<think>([\s\S]*?)<\/think>/g
        let m
        while ((m = re.exec(text)) !== null) {
            const part = (m[1] ?? "").trim()
            if (part.length > 0) {
                if (out.length > 0) out += "\n\n"
                out += part
            }
        }
        return out
    }

    function stripThinkBlocks(text) {
        if (!text || text.length === 0) return ""
        return text.replace(/<think>[\s\S]*?<\/think>/g, "").trim()
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
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                // ── Nuevo chat ────────────────────────────────────────────────
                Rectangle {
                    id: newChatBtn
                    Layout.fillWidth: true
                    height: 40
                    color: "transparent"
                    RowLayout {
                        anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                        spacing: 6
                        Text { text: "💬"; font.pixelSize: 11 }
                        Text {
                            text: "Nuevo chat"
                            color: Theme.textMuted
                            font.pixelSize: 12
                            Layout.fillWidth: true
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        onEntered: newChatBtn.color = Theme.highlight
                        onExited:  newChatBtn.color = "transparent"
                        onClicked: App.newChatSession()
                    }
                }

                // ── Nuevo proyecto ────────────────────────────────────────────
                Rectangle {
                    id: newProjectBtn
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
                        onEntered: newProjectBtn.color = Theme.highlight
                        onExited:  newProjectBtn.color = "transparent"
                        onClicked: newProjectPopup.open()
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                ListView {
                    id: sessionsList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: App.chatSessions
                    ScrollBar.vertical: LcScrollBar { policy: ScrollBar.AsNeeded }

                    section.property: "projectName"
                    section.criteria: ViewSection.FullString

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 32
                        visible: sessionsList.count === 0
                        text: "Sin chats todavía.\nUsá + para crear uno."
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }

                    section.delegate: Rectangle {
                        width: sessionsList.width
                        height: 28
                        color: Theme.baseBg

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            onClicked: {
                                projectCtxMenu.projectName = section
                                projectCtxMenu.popup()
                            }
                        }

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
                                        const pid = root.projectIdForSection(section)
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
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: (mouse) => {
                                if (mouse.button === Qt.RightButton) {
                                    ctxMenu.sessionId = modelData.id
                                    ctxMenu.sessionTitle = (modelData.title ?? "")
                                    ctxMenu.projects = App.chatProjects()
                                    ctxMenu.popup()
                                } else {
                                    App.switchChatSession(modelData.id)
                                }
                            }
                        }
                    }

                    Menu {
                        id: ctxMenu
                        property string sessionId: ""
                        property string sessionTitle: ""
                        property var projects: []

                        Menu {
                            id: moveMenu
                            title: "Mover a proyecto"
                            Instantiator {
                                model: ctxMenu.projects
                                delegate: MenuItem {
                                    text: modelData.projectName
                                    onTriggered: App.moveChatToProject(ctxMenu.sessionId,
                                                                       modelData.projectId,
                                                                       modelData.projectName)
                                }
                                onObjectAdded: (index, object) => moveMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => moveMenu.removeItem(object)
                            }
                        }
                        MenuItem {
                            text: "Renombrar chat"
                            onTriggered: if (ctxMenu.sessionId.length > 0) {
                                renameDialog.mode = "chat"
                                renameDialog.targetId = ctxMenu.sessionId
                                renameDialog.oldName = ctxMenu.sessionTitle
                                renameField.text = ctxMenu.sessionTitle
                                renameDialog.open()
                            }
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: "Borrar chat"
                            onTriggered: if (ctxMenu.sessionId.length > 0) App.deleteChatSession(ctxMenu.sessionId)
                        }
                    }

                    Menu {
                        id: projectCtxMenu
                        property string projectName: ""
                        MenuItem {
                            text: "Renombrar proyecto"
                            onTriggered: if (projectCtxMenu.projectName.length > 0) {
                                renameDialog.mode = "project"
                                renameDialog.oldName = projectCtxMenu.projectName
                                renameField.text = projectCtxMenu.projectName
                                renameDialog.open()
                            }
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: "Borrar proyecto"
                            onTriggered: if (projectCtxMenu.projectName.length > 0) {
                                deleteProjectDialog.projectName = projectCtxMenu.projectName
                                deleteProjectDialog.open()
                            }
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

            // ── Renombrar chat / proyecto ─────────────────────────────────────
            Dialog {
                id: renameDialog
                property string mode: "chat"   // "chat" | "project"
                property string targetId: ""
                property string oldName: ""
                modal: true
                parent: Overlay.overlay
                x: Math.round((parent.width - width) / 2)
                y: Math.round((parent.height - height) / 2)
                width: 420
                height: 200
                closePolicy: Popup.CloseOnEscape

                function commit() {
                    const t = renameField.text.trim()
                    if (t.length === 0) return
                    if (renameDialog.mode === "project")
                        App.renameChatProject(renameDialog.oldName, t)
                    else
                        App.renameChatSession(renameDialog.targetId, t)
                    renameDialog.close()
                }

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
                        text: renameDialog.mode === "project" ? "Renombrar proyecto" : "Renombrar chat"
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
                            onClicked: renameDialog.close()
                        }
                        LcButton {
                            text: "Guardar"
                            enabled: renameField.text.trim().length > 0
                            onClicked: renameDialog.commit()
                        }
                    }
                }

                contentItem: Item {
                    width: 380; height: 46
                    LcTextField {
                        id: renameField
                        anchors.fill: parent
                        placeholderText: "Nuevo nombre"
                        Keys.onReturnPressed: renameDialog.commit()
                    }
                }

                onOpened: { renameField.forceActiveFocus(); renameField.selectAll() }
            }

            // ── Borrar proyecto (confirmación) ────────────────────────────────
            Dialog {
                id: deleteProjectDialog
                property string projectName: ""
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
                        text: "Borrar proyecto"
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
                            onClicked: deleteProjectDialog.close()
                        }
                        LcButton {
                            text: "Borrar"
                            danger: true
                            onClicked: {
                                App.deleteChatProject(deleteProjectDialog.projectName)
                                deleteProjectDialog.close()
                            }
                        }
                    }
                }

                contentItem: Item {
                    Text {
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 20 }
                        text: "Se borrará el proyecto \"" + deleteProjectDialog.projectName
                              + "\" y todos sus chats. Esta acción no se puede deshacer."
                        color: Theme.textPrimary
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                }
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
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: !App.serverRunning ? Theme.errorText
                             : App.serverReady     ? Theme.successText
                             :                       Theme.warnText
                        SequentialAnimation on opacity {
                            running: App.serverRunning && !App.serverReady
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.3; duration: 600 }
                            NumberAnimation { to: 1.0; duration: 600 }
                        }
                    }
                    Text {
                        text: {
                            const _lang = App.langV
                            if (!App.serverRunning) return App.l("chat.serverStopped")
                            const title = App.chatSessionTitle ?? ""
                            return title.length > 0 ? title : App.activeLaunchId
                        }
                        color: !App.serverRunning ? Theme.errorText
                             : !App.serverReady   ? Theme.warnText
                             :                      Theme.textSecondary
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.fillWidth: true
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
                ScrollBar.vertical: LcScrollBar { policy: ScrollBar.AsNeeded }

                Text {
                    anchors.centerIn: parent
                    visible: App.chatMessages.length === 0 && !(App.serverRunning && !App.serverReady)
                    text: {
                        const _lang = App.langV
                        return App.serverRunning ? App.l("chat.startMessage") : App.l("chat.startServer")
                    }
                    color: Theme.textMuted
                    font.pixelSize: 14
                }

                // Popup no bloqueante: modelo cargando (centrado, sin frenar la UI)
                Rectangle {
                    anchors.centerIn: parent
                    visible: App.serverRunning && !App.serverReady
                    radius: 10
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    implicitWidth: loadingRow.implicitWidth + 32
                    implicitHeight: loadingRow.implicitHeight + 24
                    Row {
                        id: loadingRow
                        anchors.centerIn: parent
                        spacing: 10
                        Rectangle {
                            width: 10; height: 10; radius: 5
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.warnText
                            SequentialAnimation on opacity {
                                running: parent.parent.visible
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.3; duration: 600 }
                                NumberAnimation { to: 1.0; duration: 600 }
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Cargando modelo..."
                            color: Theme.textSecondary
                            font.pixelSize: 14
                        }
                    }
                }

                delegate: Item {
                    id: delegateRoot
                    width: msgList.width
                    height: bubbleRect.height + 8

                    readonly property bool isUser: modelData.role === "user"
                    readonly property string content: modelData.content ?? ""
                    readonly property bool isTyping: modelData.typing ?? false
                    readonly property string msgId: (modelData.id ?? (index + "-" + modelData.role))
                    readonly property string thinkContent: root.extractThinkContent(content)
                    readonly property bool hasThink: thinkContent.trim().length > 0
                    readonly property string visibleContentRaw: root.stripThinkBlocks(content)
                    readonly property bool thinkOpen: root.thinkExpanded[msgId] === true

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
                        height: Math.max(contentCol.implicitHeight + 22, 44)
                        radius: 10
                        color: delegateRoot.isUser ? Theme.chatUserBubble : Theme.chatAsstBubble
                        border.width: delegateRoot.isUser ? 0 : 1
                        border.color: Theme.borderColor

                        Column {
                            id: contentCol
                            anchors { top: parent.top; left: parent.left; right: parent.right; margins: 11 }
                            spacing: 8

                            Rectangle {
                                visible: delegateRoot.hasThink
                                width: parent.width
                                radius: 6
                                color: Theme.surfaceBg
                                border.color: Theme.borderColor
                                border.width: 1
                                implicitHeight: thinkHeader.height + (delegateRoot.thinkOpen ? thinkBody.implicitHeight + 8 : 0) + 8

                                Column {
                                    anchors { fill: parent; margins: 6 }
                                    spacing: 6

                                    Rectangle {
                                        id: thinkHeader
                                        width: parent.width
                                        height: 24
                                        color: "transparent"

                                        Row {
                                            anchors.fill: parent
                                            spacing: 6
                                            Text {
                                                text: delegateRoot.thinkOpen ? "▾" : "▸"
                                                color: Theme.textMuted
                                                font.pixelSize: 12
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                            Text {
                                                text: "Think"
                                                color: Theme.textMuted
                                                font.pixelSize: 12
                                                font.bold: true
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                const cur = root.thinkExpanded
                                                cur[delegateRoot.msgId] = !delegateRoot.thinkOpen
                                                root.thinkExpanded = cur
                                            }
                                        }
                                    }

                                    TextEdit {
                                        id: thinkBody
                                        visible: delegateRoot.thinkOpen
                                        width: parent.width
                                        text: delegateRoot.thinkContent
                                        color: Theme.textMuted
                                        font.family: "Consolas,monospace"
                                        font.pixelSize: 12
                                        wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                                        readOnly: true
                                        selectByMouse: true
                                    }
                                }
                            }

                            TextEdit {
                                id: msgText
                                width: parent.width
                                text: {
                                    if (delegateRoot.isTyping && delegateRoot.content.length === 0)
                                        return "⏳ Procesando..."
                                    const base = delegateRoot.visibleContentRaw.length > 0
                                        ? delegateRoot.visibleContentRaw
                                        : delegateRoot.content
                                    if (delegateRoot.isTyping)
                                        return base + "▌"
                                    return base
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
                implicitHeight: inputCol.implicitHeight + 20

                ColumnLayout {
                    id: inputCol
                    anchors { fill: parent; leftMargin: 12; rightMargin: 12; topMargin: 10; bottomMargin: 10 }
                    spacing: 6

                    // Chips de adjuntos
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: root.chatAttachments.length > 0
                        Repeater {
                            model: root.chatAttachments
                            delegate: Rectangle {
                                radius: 6
                                color: Theme.inputBg
                                border.color: Theme.borderColor
                                height: 24
                                width: chipRow.implicitWidth + 14
                                Row {
                                    id: chipRow
                                    anchors.centerIn: parent
                                    spacing: 6
                                    Text { text: "📎 " + root.fileName(modelData); color: Theme.textSecondary; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                                    Text {
                                        text: "✕"; color: Theme.textMuted; font.pixelSize: 11
                                        anchors.verticalCenter: parent.verticalCenter
                                        MouseArea {
                                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                const a = root.chatAttachments.slice()
                                                a.splice(index, 1)
                                                root.chatAttachments = a
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        CheckBox {
                            id: thinkingToggle
                            text: "Thinking"
                            visible: App.chatThinkingSupported
                            checked: root.thinkingEnabled
                            enabled: !App.chatGenerating && App.chatThinkingSupported
                            onToggled: {
                                root.thinkingEnabled = checked
                                App.setChatThinkingEnabled(checked)
                            }
                            contentItem: Text {
                                text: parent.text
                                color: Theme.textSecondary
                                leftPadding: parent.indicator.width + 6
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 12
                            }
                        }

                        LcButton {
                            text: "📎"
                            secondary: true
                            implicitWidth: 36
                            enabled: App.serverRunning && App.serverReady && !App.chatGenerating
                            onClicked: {
                                const picked = App.pickChatAttachments()
                                if (picked && picked.length > 0)
                                    root.chatAttachments = root.chatAttachments.concat(picked)
                            }
                        }

                        TextField {
                            id: inputField
                            Layout.fillWidth: true
                            placeholderText: {
                                const _lang = App.langV
                                return App.chatGenerating ? App.l("chat.generating") : App.l("chat.placeholder")
                            }
                            enabled: App.serverRunning && App.serverReady && !App.chatGenerating
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
                                if (!(event.modifiers & Qt.ShiftModifier)) { event.accepted = true; root.sendNow() }
                            }
                            // Ctrl+V: si hay imagen en el portapapeles, adjuntarla.
                            Keys.onPressed: (event) => {
                                if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_V) {
                                    const p = App.pasteClipboardImage()
                                    if (p && p.length > 0) {
                                        root.chatAttachments = root.chatAttachments.concat([p])
                                        event.accepted = true
                                    }
                                }
                            }
                        }

                        LcButton {
                            text: {
                                const _lang = App.langV
                                return App.chatGenerating ? App.l("chat.stop") : App.l("chat.send")
                            }
                            danger: App.chatGenerating
                            enabled: App.serverRunning && App.serverReady && (App.chatGenerating || inputField.text.trim().length > 0 || root.chatAttachments.length > 0)
                            onClicked: root.sendNow()
                        }
                    }
                }
            }
        }
    }
}
