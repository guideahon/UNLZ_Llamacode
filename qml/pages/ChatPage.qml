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

    // Envío normal (idle): texto + adjuntos.
    function doSend() {
        const t = inputField.text.trim()
        if (t.length === 0 && root.chatAttachments.length === 0) return
        if (root.chatAttachments.length > 0)
            App.sendChatMessageWithAttachments(t, root.chatAttachments)
        else
            App.sendChatMessage(t)
        inputField.text = ""
        root.chatAttachments = []
    }
    // Encolar: se envía cuando la respuesta actual termina (solo texto).
    function doQueue() {
        const t = inputField.text.trim()
        if (t.length === 0) return
        App.queueChat(t); inputField.text = ""
    }
    // Interrumpir: corta la respuesta actual y envía ya (solo texto).
    function doSteer() {
        const t = inputField.text.trim()
        if (t.length === 0) return
        App.steerChat(t); inputField.text = ""; root.chatAttachments = []
    }
    // Enter: enviar (idle) o encolar (generando).
    function enterPressed() {
        if (App.chatGenerating) doQueue(); else doSend()
    }

    function scrollToBottom() { msgList.followBottom = true; Qt.callLater(() => { msgList.scrollToBottom() }) }

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

    function estimateTokens(text) {
        const n = String(text ?? "").trim().length
        return n <= 0 ? 0 : Math.ceil(n / 4)
    }

    function formatDuration(ms) {
        const v = Math.max(0, Math.floor(ms || 0))
        if (v < 1000) return v + "ms"
        return (v / 1000).toFixed(2) + "s"
    }

    function formatMeta(modelData) {
        try {
            if (!modelData) return ""
            let ts = Number(modelData.completedAt ?? modelData.createdAt ?? 0)
            if (!isFinite(ts) || ts <= 0) return ""
            if (ts < 1000000000000) ts *= 1000
            const d = new Date(ts)
            if (isNaN(d.getTime())) return ""
            const pad = n => (n < 10 ? "0" : "") + n
            const date = d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate())
            const hms = pad(d.getHours()) + ":" + pad(d.getMinutes()) + ":" + pad(d.getSeconds())
            const content = String(modelData.content ?? "")
            const rawTokens = Number(modelData.tokens ?? estimateTokens(content))
            const tokens = isFinite(rawTokens) && rawTokens >= 0 ? Math.floor(rawTokens) : estimateTokens(content)
            // El mensaje del usuario no es generación de un LLM: ms/tps no aplican.
            if (String(modelData.role ?? "") === "user")
                return date + " - " + hms + " - tok " + tokens
            let elapsedMs = Number(modelData.elapsedMs ?? 0)
            if (!isFinite(elapsedMs) || elapsedMs < 0) {
                const started = Number(modelData.createdAt ?? 0)
                const ended = Number(modelData.completedAt ?? 0)
                elapsedMs = (isFinite(started) && isFinite(ended) && ended >= started) ? (ended - started) : 0
            }
            const rawTps = Number(modelData.tps ?? (elapsedMs > 0 ? (tokens * 1000.0 / elapsedMs) : 0))
            const tps = isFinite(rawTps) && rawTps >= 0 ? rawTps : 0
            return date + " - " + hms + " - tok " + tokens + " - " + formatDuration(elapsedMs) + " - " + tps.toFixed(2) + " tps"
        } catch (e) {
            return ""
        }
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
                        MenuItem {
                            text: "Exportar a Markdown"
                            onTriggered: if (ctxMenu.sessionId.length > 0) App.exportChatSession(ctxMenu.sessionId, "md")
                        }
                        MenuItem {
                            text: "Exportar a JSON"
                            onTriggered: if (ctxMenu.sessionId.length > 0) App.exportChatSession(ctxMenu.sessionId, "json")
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
                boundsBehavior: Flickable.StopAtBounds
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
                    readonly property string metaLine: root.formatMeta(modelData)

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
                            Text {
                                visible: metaLine.length > 0
                                width: parent.width
                                text: metaLine
                                color: Theme.textMuted
                                font.pixelSize: 10
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideRight
                            }
                            // Copiar mensaje al portapapeles.
                            Row {
                                width: parent.width
                                layoutDirection: Qt.RightToLeft
                                visible: delegateRoot.content.length > 0 && !delegateRoot.isTyping
                                Text {
                                    text: bubbleRect.justCopied ? "✓ Copiado" : "⧉ Copiar"
                                    color: copyMA.containsMouse || bubbleRect.justCopied
                                           ? Theme.textPrimary : Theme.textMuted
                                    font.pixelSize: 10
                                    MouseArea {
                                        id: copyMA
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            App.copyToClipboard(delegateRoot.content)
                                            bubbleRect.justCopied = true
                                            chatCopyResetTimer.restart()
                                        }
                                    }
                                }
                            }
                        }

                        property bool justCopied: false
                        Timer { id: chatCopyResetTimer; interval: 1500; onTriggered: bubbleRect.justCopied = false }
                    }

                    Component.onCompleted: { msgList.followBottom = true; Qt.callLater(() => { msgList.scrollToBottom() }) }
                }

                // Auto-scroll por contentY directo (no positionViewAtEnd) para
                // evitar oscilación con delegates de altura variable.
                property bool followBottom: true

                function scrollToBottom() {
                    var maxY = Math.max(0, contentHeight - height)
                    if (contentY !== maxY)
                        contentY = maxY
                }

                onMovementEnded: followBottom = atYEnd
                onContentHeightChanged: if (followBottom) chatBottomTimer.restart()
                onCountChanged: { followBottom = true; chatBottomTimer.restart() }
                onModelChanged: { followBottom = true; chatBottomTimer.restart() }

                Timer {
                    id: chatBottomTimer
                    interval: 16
                    onTriggered: if (msgList.followBottom) msgList.scrollToBottom()
                }

                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: function(ev) {
                        var step = ev.angleDelta.y / 120 * 90
                        var maxY = Math.max(0, msgList.contentHeight - msgList.height)
                        msgList.contentY = Math.max(0, Math.min(maxY, msgList.contentY - step))
                        msgList.followBottom = (msgList.contentY >= maxY - 2)
                        ev.accepted = true
                    }
                }
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
                                return App.chatGenerating
                                    ? ("Enter encola · Shift+Enter interrumpe"
                                       + (App.chatQueuedCount > 0 ? "  ·  " + App.chatQueuedCount + " en cola" : ""))
                                    : App.l("chat.placeholder")
                            }
                            // Habilitado también mientras genera, para poder encolar/dirigir.
                            enabled: App.serverRunning && App.serverReady
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
                            // Enter = enviar/encolar. Shift+Enter = interrumpir (campo de una línea).
                            Keys.onReturnPressed: (event) => {
                                event.accepted = true
                                if (event.modifiers & Qt.ShiftModifier) root.doSteer()
                                else root.enterPressed()
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

                        // Idle: enviar.
                        LcButton {
                            visible: !App.chatGenerating
                            text: (App.langV, App.l("chat.send"))
                            enabled: App.serverRunning && App.serverReady
                                && (inputField.text.trim().length > 0 || root.chatAttachments.length > 0)
                            onClicked: root.doSend()
                        }
                        // Generando + hay texto: encolar / interrumpir.
                        LcButton {
                            visible: App.chatGenerating && inputField.text.trim().length > 0
                            text: "Encolar" + (App.chatQueuedCount > 0 ? " (" + App.chatQueuedCount + ")" : "")
                            onClicked: root.doQueue()
                        }
                        LcButton {
                            visible: App.chatGenerating && inputField.text.trim().length > 0
                            text: "Interrumpir"
                            danger: true
                            onClicked: root.doSteer()
                        }
                        // PARAR: cortar sin enviar.
                        LcButton {
                            visible: App.chatGenerating
                            text: (App.langV, App.l("chat.stop"))
                            danger: true
                            onClicked: App.stopChatGeneration()
                        }
                    }
                }
            }
        }
    }
}
