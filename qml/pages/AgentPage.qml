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
    property double lastAgentActivityMs: Date.now()
    property int idleSeconds: 0

    readonly property bool waitingApproval: (App.agentPendingTool.id ?? "").length > 0
    readonly property bool hasTypingMessage: {
        for (let i = 0; i < App.agentMessages.length; i++) {
            if ((App.agentMessages[i].typing ?? false) === true) return true
        }
        return false
    }
    readonly property string activityLabel: {
        if (!App.agentRunning) return "Detenido"
        if (waitingApproval) return "Esperando aprobación"
        if (hasTypingMessage) {
            if (idleSeconds < 12) return "Procesando"
            return "Sin actividad (" + idleSeconds + "s)"
        }
        return "Listo"
    }
    readonly property color activityColor: {
        if (!App.agentRunning) return Theme.textMuted
        if (waitingApproval) return Theme.warnText
        if (hasTypingMessage && idleSeconds >= 12) return Theme.errorText
        if (hasTypingMessage) return Theme.successText
        return Theme.textSecondary
    }

    // Confirmación para activar el modo "Super Agente" (acceso total al disco).
    Dialog {
        id: superAgentDialog
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 460
        height: 260
        closePolicy: Popup.CloseOnEscape

        // Cancelar/cerrar: revertir el combo al modo real actual.
        function revertCombo() {
            approvalModeCombo.currentIndex =
                App.agentApprovalMode === "manual" ? 2
                : App.agentApprovalMode === "auto" ? 0
                : App.agentApprovalMode === "super" ? 3 : 1
        }
        onRejected: revertCombo()

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
                text: "⚠  Modo Super Agente — riesgoso"
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
                    onClicked: { superAgentDialog.revertCombo(); superAgentDialog.close() }
                }
                LcButton {
                    text: "Activar"
                    danger: true
                    onClicked: { App.agentApprovalMode = "super"; superAgentDialog.close() }
                }
            }
        }

        contentItem: Item {
            Text {
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 20 }
                text: "El Super Agente tendrá acceso de LECTURA y ESCRITURA a TODAS las " +
                      "carpetas de la computadora y ejecutará comandos de shell SIN pedir " +
                      "permiso.\n\nPuede modificar o borrar archivos fuera del proyecto. " +
                      "Usalo solo si confiás en lo que le pedís.\n\n¿Activar Super Agente?"
                color: Theme.textPrimary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
        }
    }

    // La página vive en un StackLayout (no se recrea): al mostrarse, sincronizar
    // con el perfil que se lanzó en "Lanzar" (App.activeLaunchId) y re-resolver
    // el harness por si cambió en Perfiles.
    onVisibleChanged: if (visible) { syncToActiveLaunch(); if (selectedLaunchId.length > 0) resolveHarness(selectedLaunchId) }

    // Selecciona en el combo el launch activo (el que se inició en Lanzar).
    // No pisa la selección si el agente ya está corriendo.
    function syncToActiveLaunch() {
        if (App.agentRunning) return
        const id = App.activeLaunchId
        if (!id || id.length === 0) return
        if (id === selectedLaunchId) return
        selectedLaunchId = id
        profileCombo.currentIndex = profileCombo.indexOfValue(id)
        resolveHarness(id)
    }

    function projectDirForSection(sectionName) {
        for (let i = 0; i < App.agentSessions.length; i++) {
            if ((App.agentSessions[i].projectName ?? "") === sectionName)
                return App.agentSessions[i].projectDir ?? ""
        }
        return ""
    }

    function resolveHarness(launchId) {
        if (!launchId || launchId.length === 0) {
            resolvedAdapter = "none"; resolvedAdapterLabel = ""; return
        }
        const lp = App.profileManager.getLaunchProfile(launchId)
        const harnessId = lp.harnessProfileId ?? ""
        if (harnessId.length > 0) {
            const hp = App.profileManager.getHarness(harnessId)
            resolvedAdapter = hp.adapter ?? "none"
            // Mostrar el nombre visible del harness (ej. "LlamaAgent"), no el id interno.
            resolvedAdapterLabel = (hp.name && hp.name.length > 0) ? hp.name : (hp.adapter ?? "")
        } else {
            resolvedAdapter = "none"; resolvedAdapterLabel = ""
        }
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
            if (String(modelData.role ?? "") === "diff") return ""
            let ts = Number(modelData.completedAt ?? modelData.createdAt ?? 0)
            if (!isFinite(ts) || ts <= 0) return ""
            if (ts < 1000000000000) ts *= 1000 // si vino en segundos
            const d = new Date(ts)
            if (isNaN(d.getTime())) return ""
            const pad = n => (n < 10 ? "0" : "") + n
            const date = d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate())
            const hms = pad(d.getHours()) + ":" + pad(d.getMinutes()) + ":" + pad(d.getSeconds())
            const content = String(modelData.content ?? "")
            const rawTokens = Number(modelData.tokens ?? estimateTokens(content))
            const tokens = isFinite(rawTokens) && rawTokens >= 0 ? Math.floor(rawTokens) : estimateTokens(content)
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

    Component.onCompleted: {
        // Preferir el launch activo (lanzado en "Lanzar"); si no hay, el primero.
        let target = App.activeLaunchId
        if (!target || target.length === 0) {
            if (App.profileManager.launchProfiles.rowCount() > 0) {
                const idx = App.profileManager.launchProfiles.index(0, 0)
                target = App.profileManager.launchProfiles.data(idx, 257) ?? ""
            }
        }
        if (target && target.length > 0) {
            selectedLaunchId = target
            profileCombo.currentIndex = profileCombo.indexOfValue(target)
            resolveHarness(target)
        }
    }

    function markActivity() {
        lastAgentActivityMs = Date.now()
        idleSeconds = 0
    }

    Connections {
        target: App
        // Cuando se lanza un perfil en "Lanzar", reflejarlo acá.
        function onActiveLaunchIdChanged() { root.syncToActiveLaunch() }
        function onAgentMessagesChanged() { root.markActivity() }
        function onAgentPendingToolChanged() { root.markActivity() }
        function onAgentLogChanged() { root.markActivity() }
        function onAgentRunningChanged() { root.markActivity() }
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: {
            if (!App.agentRunning) {
                idleSeconds = 0
                return
            }
            idleSeconds = Math.floor((Date.now() - lastAgentActivityMs) / 1000)
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
                Rectangle {
                    visible: App.agentRunning
                    implicitWidth: activityText.implicitWidth + 16
                    height: 24
                    radius: 6
                    color: Theme.inputBg
                    border.color: root.activityColor
                    border.width: 1
                    Text {
                        id: activityText
                        anchors.centerIn: parent
                        text: root.activityLabel
                        color: root.activityColor
                        font.pixelSize: 11
                    }
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

                // Indicador de contexto (tokens usados / n_ctx).
                Rectangle {
                    visible: App.agentContextLimit > 0 && App.agentRunning
                    implicitWidth: 130; implicitHeight: 26
                    radius: 6
                    color: Theme.inputBg
                    border.color: Theme.borderColor
                    readonly property real frac: App.agentContextLimit > 0
                        ? Math.min(1, App.agentContextUsed / App.agentContextLimit) : 0
                    Rectangle {
                        anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 1 }
                        width: (parent.width - 2) * parent.frac
                        radius: 6
                        color: parent.frac > 0.9 ? Theme.errorText
                             : parent.frac > 0.7 ? Theme.warnText : Theme.accent
                        opacity: 0.35
                    }
                    Text {
                        anchors.centerIn: parent
                        text: "ctx " + App.agentContextUsed + "/" + App.agentContextLimit
                        color: Theme.textSecondary; font { pixelSize: 11; family: "Consolas,monospace" }
                    }
                }

                // Política de aprobación de herramientas.
                ComboBox {
                    id: approvalModeCombo
                    visible: resolvedAdapter === "opencode" || resolvedAdapter === "llamaagent"
                    implicitWidth: 150
                    model: [
                        { key: "auto",   label: "Aprobar todo" },
                        { key: "ask",    label: "Pedir escritura" },
                        { key: "manual", label: "Pedir todo" },
                        { key: "super",  label: "Super Agente ⚠" }
                    ]
                    textRole: "label"
                    valueRole: "key"
                    // indexOfValue() no es fiable con model de objetos JS (devuelve -1
                    // y el combo cae a índice 0 mostrando "Aprobar todo" aunque el modo
                    // real sea "ask"). Mapeo explícito para que UI y backend coincidan.
                    currentIndex: App.agentApprovalMode === "manual" ? 2
                                  : App.agentApprovalMode === "auto" ? 0
                                  : App.agentApprovalMode === "super" ? 3 : 1
                    onActivated: {
                        if (currentValue === "super" && App.agentApprovalMode !== "super") {
                            superAgentDialog.open()   // confirmar antes de aplicar
                        } else {
                            App.agentApprovalMode = currentValue
                        }
                    }
                    background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                    contentItem: Text {
                        text: approvalModeCombo.displayText
                        color: Theme.textPrimary; font.pixelSize: 12
                        leftPadding: 10; verticalAlignment: Text.AlignVCenter
                    }
                }
                // Razonamiento del agente (default OFF para respetar perfiles con
                // --reasoning off y reducir carga del server).
                CheckBox {
                    id: agentThinkingCheck
                    visible: resolvedAdapter === "llamaagent"
                    text: "Pensar"
                    checked: App.agentThinkingEnabled
                    onToggled: App.agentThinkingEnabled = checked
                    contentItem: Text {
                        text: agentThinkingCheck.text
                        color: Theme.textPrimary; font.pixelSize: 12
                        leftPadding: agentThinkingCheck.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                LcButton {
                    text: "🧠 Memoria"
                    secondary: true
                    visible: (resolvedAdapter === "llamaagent" || resolvedAdapter === "opencode")
                    onClicked: {
                        memoryDialog.text = App.readAgentMemory("")
                        memoryDialog.open()
                    }
                }
                LcButton {
                    text: "⚙ Agente"
                    secondary: true
                    visible: resolvedAdapter === "llamaagent"
                    onClicked: {
                        agentTuningSystem.text = App.agentSystemPrompt
                        agentTuningTemp.text = App.agentTemperature >= 0 ? String(App.agentTemperature) : ""
                        agentTuningDialog.open()
                    }
                }
                LcButton {
                    text: "⚙ Config opencode"
                    secondary: true
                    visible: resolvedAdapter === "opencode"
                    onClicked: {
                        opencodeConfigDialog.projectDir = App.currentAgentProjectDir()
                        opencodeConfigDialog.open()
                    }
                }
                LcButton {
                    text: "Ver log nativo"
                    secondary: true
                    visible: resolvedAdapter.length > 0 && resolvedAdapter !== "none"
                    onClicked: App.openAgentLogDir(resolvedAdapter)
                }
                LcButton {
                    text: "Abrir logs runtime"
                    secondary: true
                    onClicked: App.openRuntimeLogDir()
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
                                text: "↻"
                                secondary: true
                                implicitWidth: 28
                                implicitHeight: 24
                                onClicked: App.refreshOpencodeSessionList()
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                    // ── Nueva sesión ──────────────────────────────────────────
                    Rectangle {
                        id: newSessionBtn
                        Layout.fillWidth: true
                        height: 40
                        color: "transparent"
                        RowLayout {
                            anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                            spacing: 6
                            Text { text: "💬"; font.pixelSize: 11 }
                            Text {
                                text: "Nueva sesión"
                                color: Theme.textMuted
                                font.pixelSize: 12
                                Layout.fillWidth: true
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true
                            onEntered: newSessionBtn.color = Theme.highlight
                            onExited:  newSessionBtn.color = "transparent"
                            onClicked: App.newOpencodeSession()
                        }
                    }

                    // ── Nuevo proyecto ────────────────────────────────────────
                    Rectangle {
                        id: newProjectBtnTop
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
                            onEntered: newProjectBtnTop.color = Theme.highlight
                            onExited:  newProjectBtnTop.color = "transparent"
                            onClicked: {
                                const dir = App.pickDirectory("Elegí la carpeta del proyecto")
                                if (dir.length === 0) return
                                App.changeAgentProject(dir)
                                // Si el agente no está corriendo, arrancarlo en esa carpeta.
                                if (!App.agentRunning && selectedLaunchId.length > 0)
                                    App.startAgent(selectedLaunchId)
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
                        ScrollBar.vertical: LcScrollBar { policy: ScrollBar.AsNeeded }

                        section.property: "projectName"
                        section.criteria: ViewSection.FullString

                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 32
                            visible: sessionsList.count === 0
                            text: "Sin sesiones todavía.\nUsá + o ↻ para empezar."
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            color: Theme.textMuted
                            font.pixelSize: 11
                        }

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
                                        onClicked: App.newOpencodeSessionInProject(root.projectDirForSection(section))
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
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: (mouse) => {
                                    if (mouse.button === Qt.RightButton) {
                                        sessionCtxMenu.sessionId = modelData.id
                                        sessionCtxMenu.sessionTitle = (modelData.title ?? "")
                                        sessionCtxMenu.popup()
                                    } else {
                                        App.switchOpencodeSession(modelData.id)
                                    }
                                }
                            }
                        }

                        Menu {
                            id: sessionCtxMenu
                            property string sessionId: ""
                            property string sessionTitle: ""
                            MenuItem {
                                text: "Renombrar sesión"
                                onTriggered: if (sessionCtxMenu.sessionId.length > 0) {
                                    agentRenameDialog.targetId = sessionCtxMenu.sessionId
                                    agentRenameField.text = sessionCtxMenu.sessionTitle
                                    agentRenameDialog.open()
                                }
                            }
                            MenuItem {
                                text: "Bifurcar (fork)"
                                onTriggered: if (sessionCtxMenu.sessionId.length > 0)
                                    App.forkOpencodeSession(sessionCtxMenu.sessionId)
                            }
                            MenuSeparator {}
                            MenuItem {
                                text: "Borrar sesión"
                                onTriggered: if (sessionCtxMenu.sessionId.length > 0) {
                                    agentDeleteDialog.targetId = sessionCtxMenu.sessionId
                                    agentDeleteDialog.sessionTitle = sessionCtxMenu.sessionTitle
                                    agentDeleteDialog.open()
                                }
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

                // Estado bloqueado: servidor caído o modelo aún cargando.
                Rectangle {
                    anchors.centerIn: parent
                    z: 5
                    visible: App.agentRunning && (!App.serverRunning || !App.serverReady)
                    radius: 10
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    implicitWidth: agentLoadingRow.implicitWidth + 32
                    implicitHeight: agentLoadingRow.implicitHeight + 24
                    Row {
                        id: agentLoadingRow
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
                            text: !App.serverRunning
                                ? "Servidor no disponible. Iniciá el modelo en Lanzar."
                                : "Cargando modelo..."
                            color: Theme.textSecondary; font.pixelSize: 14
                        }
                    }
                }

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
                    ScrollBar.vertical: LcScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Item {
                        id: delegateRoot
                        width: msgList.width
                        height: (isDiff ? diffCard.height : bubbleRect.height) + 8

                        readonly property bool isUser: modelData.role === "user"
                        readonly property bool isDiff: modelData.role === "diff"
                        readonly property string content: modelData.content ?? ""
                        readonly property bool isTyping: modelData.typing ?? false
                        readonly property string metaLine: root.formatMeta(modelData)

                        Rectangle {
                            id: bubbleRect
                            visible: !delegateRoot.isDiff
                            anchors {
                                top: parent.top
                                right: delegateRoot.isUser ? parent.right : undefined
                                rightMargin: delegateRoot.isUser ? 16 : undefined
                                left: delegateRoot.isUser ? undefined : parent.left
                                leftMargin: delegateRoot.isUser ? undefined : 16
                            }
                            width: Math.min(delegateRoot.width - 80, delegateRoot.width * 0.78)
                            height: Math.max(msgCol.implicitHeight + 22, 44)
                            radius: 10
                            color: delegateRoot.isUser ? Theme.chatUserBubble : Theme.chatAsstBubble
                            border.width: delegateRoot.isUser ? 0 : 1
                            border.color: Theme.borderColor

                            Column {
                                id: msgCol
                                anchors { top: parent.top; left: parent.left; right: parent.right; margins: 11 }
                                spacing: 6

                                TextEdit {
                                    id: msgText
                                    width: parent.width
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
                                Text {
                                    visible: delegateRoot.metaLine.length > 0
                                    width: parent.width
                                    text: delegateRoot.metaLine
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                    horizontalAlignment: Text.AlignRight
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        // ── Entrada de diff (edición de archivo) ──────────────
                        Rectangle {
                            id: diffCard
                            visible: delegateRoot.isDiff
                            anchors { top: parent.top; left: parent.left; right: parent.right
                                      leftMargin: 16; rightMargin: 16 }
                            height: visible ? diffCol.implicitHeight + 20 : 0
                            radius: 8
                            color: Theme.inputBg
                            border.color: Theme.borderColor
                            readonly property bool reverted: modelData.reverted ?? false
                            // Diff colapsado por defecto: solo nombre + toggle.
                            property bool expanded: false
                            readonly property int diffLines: {
                                const d = String(modelData.diff ?? "")
                                if (d.length === 0) return 0
                                return d.split("\n").length
                            }

                            ColumnLayout {
                                id: diffCol
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    // Toggle expandir/colapsar (chevron). Clic en todo el header también abre.
                                    Text {
                                        text: diffCard.expanded ? "▾" : "▸"
                                        color: Theme.textMuted
                                        font.pixelSize: 12
                                        MouseArea {
                                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                            onClicked: diffCard.expanded = !diffCard.expanded
                                        }
                                    }
                                    Text { text: "📝"; font.pixelSize: 13 }
                                    Text {
                                        Layout.fillWidth: true
                                        text: (modelData.path ?? "")
                                              + (diffCard.diffLines > 0 ? "  · " + diffCard.diffLines + " líneas" : "")
                                              + (diffCard.reverted ? "  · revertido" : "")
                                        color: diffCard.reverted ? Theme.textMuted : Theme.textPrimary
                                        font { family: "Consolas,monospace"; pixelSize: 12; bold: true }
                                        elide: Text.ElideMiddle
                                        MouseArea {
                                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                            onClicked: diffCard.expanded = !diffCard.expanded
                                        }
                                    }
                                    LcButton {
                                        text: diffCard.expanded ? "Ocultar" : "Ver"
                                        secondary: true
                                        implicitHeight: 24
                                        visible: diffCard.diffLines > 0
                                        onClicked: diffCard.expanded = !diffCard.expanded
                                    }
                                    LcButton {
                                        text: "Revertir"; secondary: true
                                        visible: !diffCard.reverted
                                        implicitHeight: 24
                                        onClicked: App.revertAgentEdit(modelData.absPath ?? modelData.path ?? "")
                                    }
                                }

                                // Cuerpo del diff: oculto por defecto, scrollable al abrir.
                                ScrollView {
                                    Layout.fillWidth: true
                                    visible: diffCard.expanded
                                    clip: true
                                    Layout.preferredHeight: visible
                                        ? Math.min(diffText.implicitHeight + 8, 280)
                                        : 0
                                    ScrollBar.horizontal.policy: ScrollBar.AsNeeded
                                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                                    TextEdit {
                                        id: diffText
                                        text: modelData.diff ?? ""
                                        color: Theme.textSecondary
                                        font { family: "Consolas,monospace"; pixelSize: 11 }
                                        wrapMode: TextEdit.NoWrap
                                        readOnly: true; selectByMouse: true
                                        opacity: diffCard.reverted ? 0.5 : 1.0
                                    }
                                }
                            }
                        }

                        Component.onCompleted: Qt.callLater(() => { msgList.positionViewAtEnd() })
                    }

                    onCountChanged: Qt.callLater(() => { msgList.positionViewAtEnd() })
                }

                // ── Tarjeta de aprobación de herramienta (human-in-the-loop) ──
                Rectangle {
                    id: approvalCard
                    readonly property var tool: App.agentPendingTool
                    readonly property string toolId: tool.id ?? ""
                    readonly property string kind: tool.kind ?? "read"
                    visible: toolId.length > 0
                    anchors {
                        left: parent.left; right: parent.right; bottom: parent.bottom
                        margins: 12
                    }
                    height: visible ? approvalCol.implicitHeight + 24 : 0
                    radius: 10
                    color: Theme.surfaceBg
                    border.width: 2
                    border.color: kind === "shell" ? Theme.errorText
                                : kind === "write" ? Theme.warnText
                                :                    Theme.borderColor

                    ColumnLayout {
                        id: approvalCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: approvalCard.kind === "shell" ? "🖥️"
                                    : approvalCard.kind === "write" ? "✏️" : "📄"
                                font.pixelSize: 16
                            }
                            Text {
                                text: "Aprobar herramienta"
                                color: Theme.textPrimary; font { pixelSize: 13; bold: true }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: (approvalCard.tool.tool ?? "")
                                      + (approvalCard.tool.title ? " · " + approvalCard.tool.title : "")
                                color: Theme.textMuted; font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            visible: (approvalCard.tool.detail ?? "").length > 0
                            color: Theme.inputBg; radius: 6
                            border.color: Theme.borderColor
                            implicitHeight: detailText.implicitHeight + 16
                            TextEdit {
                                id: detailText
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                                text: approvalCard.tool.detail ?? ""
                                color: Theme.textSecondary
                                font { family: "Consolas,monospace"; pixelSize: 12 }
                                wrapMode: TextEdit.WrapAnywhere
                                readOnly: true; selectByMouse: true
                            }
                        }

                        // Preview de diff (solo write_file).
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(diffPreview.implicitHeight + 16, 180)
                            visible: (approvalCard.tool.diff ?? "").length > 0
                            color: Theme.baseBg; radius: 6
                            border.color: Theme.borderColor
                            clip: true
                            Flickable {
                                anchors { fill: parent; margins: 8 }
                                contentHeight: diffPreview.implicitHeight
                                contentWidth: width
                                clip: true
                                ScrollBar.vertical: LcScrollBar { policy: ScrollBar.AsNeeded }
                                TextEdit {
                                    id: diffPreview
                                    width: parent.width
                                    text: approvalCard.tool.diff ?? ""
                                    color: Theme.textSecondary
                                    font { family: "Consolas,monospace"; pixelSize: 11 }
                                    wrapMode: TextEdit.NoWrap
                                    readOnly: true; selectByMouse: true
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Item { Layout.fillWidth: true }
                            LcButton {
                                text: "Rechazar"; danger: true
                                onClicked: App.rejectAgentTool(approvalCard.toolId)
                            }
                            LcButton {
                                text: "Aprobar"
                                onClicked: App.approveAgentTool(approvalCard.toolId, false)
                            }
                            LcButton {
                                text: "Siempre"; secondary: true
                                onClicked: App.approveAgentTool(approvalCard.toolId, true)
                            }
                        }
                    }
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
                    enabled: App.serverRunning && App.serverReady
                    placeholderText: (!App.serverRunning)
                        ? "Servidor no disponible. Iniciá el modelo en Lanzar."
                        : (App.serverReady
                        ? (App.langV, App.l("agent.input"))
                        : "Modelo cargando...")
                    onAccepted: {
                        const t = text.trim()
                        if (t.length === 0 || !App.serverRunning || !App.serverReady) return
                        if (root.hasTypingMessage) return   // generando: no enviar (usar PARAR)
                        App.sendToAgent(t); text = ""
                    }
                }
                LcButton {
                    // Mientras la IA piensa/genera, el botón pasa a "PARAR" (rojo)
                    // y aborta el turno en curso. Si no, envía el mensaje.
                    readonly property bool busy: root.hasTypingMessage
                    text: busy ? "PARAR" : (App.langV, App.l("agent.send"))
                    danger: busy
                    enabled: busy
                        ? true
                        : (agentInput.text.trim().length > 0 && App.serverRunning && App.serverReady)
                    onClicked: {
                        if (busy) { App.cancelAgentGeneration(); return }
                        const t = agentInput.text.trim()
                        if (t.length === 0) return
                        App.sendToAgent(t); agentInput.text = ""
                    }
                }
            }
        }
    }

    // ── Renombrar sesión ──────────────────────────────────────────────────────
    Dialog {
        id: agentRenameDialog
        property string targetId: ""
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 420
        height: 200
        closePolicy: Popup.CloseOnEscape

        function commit() {
            const t = agentRenameField.text.trim()
            if (t.length === 0) return
            App.renameOpencodeSession(agentRenameDialog.targetId, t)
            agentRenameDialog.close()
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
                text: "Renombrar sesión"
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
                    onClicked: agentRenameDialog.close()
                }
                LcButton {
                    text: "Guardar"
                    enabled: agentRenameField.text.trim().length > 0
                    onClicked: agentRenameDialog.commit()
                }
            }
        }

        contentItem: Item {
            width: 380; height: 46
            LcTextField {
                id: agentRenameField
                anchors.fill: parent
                placeholderText: "Nuevo nombre"
                Keys.onReturnPressed: agentRenameDialog.commit()
            }
        }

        onOpened: { agentRenameField.forceActiveFocus(); agentRenameField.selectAll() }
    }

    // ── Borrar sesión (confirmación) ──────────────────────────────────────────
    Dialog {
        id: agentDeleteDialog
        property string targetId: ""
        property string sessionTitle: ""
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
                text: "Borrar sesión"
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
                    onClicked: agentDeleteDialog.close()
                }
                LcButton {
                    text: "Borrar"
                    danger: true
                    onClicked: {
                        App.deleteOpencodeSession(agentDeleteDialog.targetId)
                        agentDeleteDialog.close()
                    }
                }
            }
        }

        contentItem: Item {
            Text {
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 20 }
                text: "Se borrará la sesión \"" + agentDeleteDialog.sessionTitle
                      + "\". Esta acción no se puede deshacer."
                color: Theme.textPrimary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
        }
    }

    OpencodeConfigDialog { id: opencodeConfigDialog }

    // ── Ajustes del agente (LlamaAgentBackend): system prompt + temperatura ─────
    Dialog {
        id: agentTuningDialog
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 580
        height: 440
        closePolicy: Popup.CloseOnEscape

        background: Rectangle {
            color: Theme.popupBg; radius: 12
            border.color: Theme.popupBorderColor; border.width: 1
        }
        Overlay.modal: Rectangle { color: Theme.overlayColor }

        header: Rectangle {
            color: Theme.popupHeaderBg; height: 56; radius: 12
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
            Column {
                anchors { left: parent.left; leftMargin: 22; verticalCenter: parent.verticalCenter }
                spacing: 2
                Text { text: "Ajustes del agente"; color: Theme.textPrimary; font.pixelSize: 14; font.bold: true }
                Text { text: "instrucciones extra + temperatura para tool-calling"; color: Theme.textMuted; font.pixelSize: 11 }
            }
        }

        footer: Rectangle {
            color: Theme.popupHeaderBg; height: 56; radius: 12
            Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
            Row {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                spacing: 10
                LcButton { text: "Cancelar"; secondary: true; onClicked: agentTuningDialog.close() }
                LcButton {
                    text: "Guardar"
                    onClicked: {
                        App.agentSystemPrompt = agentTuningSystem.text
                        const t = parseFloat(agentTuningTemp.text)
                        App.agentTemperature = (agentTuningTemp.text.trim().length > 0 && !isNaN(t)) ? t : -1
                        agentTuningDialog.close()
                    }
                }
            }
        }

        contentItem: ColumnLayout {
            spacing: 10

            Text { text: "Instrucciones del agente (system prompt extra):"; color: Theme.textSecondary; font.pixelSize: 12 }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                color: Theme.inputBg; radius: 8; border.color: Theme.borderColor; clip: true
                ScrollView {
                    anchors.fill: parent; anchors.margins: 2
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    TextArea {
                        id: agentTuningSystem
                        placeholderText: "p.ej. priorizá cambios mínimos, corré tests antes de terminar, no toques archivos de config…"
                        color: Theme.textPrimary; placeholderTextColor: Theme.textMuted
                        font { family: "Consolas,monospace"; pixelSize: 12 }
                        wrapMode: TextArea.WrapAtWordBoundaryOrAnywhere
                        background: null; padding: 10; selectByMouse: true
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Text { text: "Temperatura:"; color: Theme.textSecondary; font.pixelSize: 12 }
                LcTextField {
                    id: agentTuningTemp
                    Layout.preferredWidth: 100
                    placeholderText: "auto"
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
                Text {
                    text: "vacío = default del server. Para tool-calling estable: 0.0–0.3"
                    color: Theme.textMuted; font.pixelSize: 11; Layout.fillWidth: true
                }
            }
        }
    }

    // ── Editor de memoria por proyecto (.llamacode/memory.md) ──────────────
    Dialog {
        id: memoryDialog
        property alias text: memoryArea.text
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 620
        height: 480
        closePolicy: Popup.CloseOnEscape

        background: Rectangle {
            color: Theme.popupBg; radius: 12
            border.color: Theme.popupBorderColor; border.width: 1
        }
        Overlay.modal: Rectangle { color: Theme.overlayColor }

        header: Rectangle {
            color: Theme.popupHeaderBg; height: 56; radius: 12
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
            Column {
                anchors { left: parent.left; leftMargin: 22; verticalCenter: parent.verticalCenter }
                spacing: 2
                Text { text: "Memoria del proyecto"; color: Theme.textPrimary; font.pixelSize: 14; font.bold: true }
                Text {
                    text: ".llamacode/memory.md · se inyecta en el system prompt del agente"
                    font.pixelSize: 11; color: Theme.textMuted
                }
            }
        }

        footer: Rectangle {
            color: Theme.popupHeaderBg; height: 56; radius: 12
            Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
            Row {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                spacing: 10
                LcButton { text: "Cancelar"; secondary: true; onClicked: memoryDialog.close() }
                LcButton {
                    text: "Guardar"
                    onClicked: { App.writeAgentMemory("", memoryArea.text); memoryDialog.close() }
                }
            }
        }

        contentItem: Rectangle {
            color: Theme.inputBg; radius: 8
            border.color: Theme.borderColor
            clip: true
            ScrollView {
                anchors.fill: parent; anchors.margins: 2
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                TextArea {
                    id: memoryArea
                    placeholderText: "Convenciones, comandos de build/test, arquitectura, do/don't del proyecto…"
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textMuted
                    font { family: "Consolas,monospace"; pixelSize: 12 }
                    wrapMode: TextArea.WrapAtWordBoundaryOrAnywhere
                    background: null
                    padding: 10
                    selectByMouse: true
                }
            }
        }
    }
}
