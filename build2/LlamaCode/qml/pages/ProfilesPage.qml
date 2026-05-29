import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Item {
    id: root

    property string selectedLaunchId: ""
    property string backendId: ""
    property string modelProfileId: ""
    property string runtimeId: ""
    property string backendNameCurrent: ""
    property string modelNameCurrent: ""
    property string runtimeNameCurrent: ""

    function splitArgs(raw) {
        const lines = raw.split("\n")
        const out = []
        for (let i = 0; i < lines.length; ++i) {
            const t = lines[i].trim()
            if (t.length > 0) out.push(t)
        }
        return out
    }

    function argsFlat() {
        return splitArgs(extraArgsArea.text)
    }

    function findArgValue(flag) {
        const a = argsFlat()
        for (let i = 0; i < a.length - 1; ++i) {
            if (a[i] === flag) return a[i + 1]
        }
        return ""
    }

    function hasFlag(flag) {
        const a = argsFlat()
        for (let i = 0; i < a.length; ++i) if (a[i] === flag) return true
        return false
    }

    function extractManualArgs(rawArgs) {
        const pairFlags = {
            "--alias": true, "--n-predict": true, "--cache-type-v": true, "--temp": true,
            "--top-p": true, "--top-k": true, "--repeat-penalty": true, "--presence-penalty": true,
            "--cache-ram": true, "--cache-reuse": true
        }
        const boolFlags = { "--no-context-shift": true, "--metrics": true, "--no-warmup": true }
        const out = []
        for (let i = 0; i < rawArgs.length; ++i) {
            const cur = rawArgs[i]
            if (pairFlags[cur]) { i += 1; continue }
            if (boolFlags[cur]) continue
            out.push(cur)
        }
        return out
    }

    function loadLaunch() {
        if (!selectedLaunchId || selectedLaunchId.length === 0) return

        const lp = App.profileManager.getLaunchProfile(selectedLaunchId)
        if (!lp || !lp.id) return

        backendId = lp.backendProfileId ?? ""
        modelProfileId = lp.modelProfileId ?? ""
        runtimeId = lp.runtimePresetId ?? ""
        const rawExtra = (lp.extraArgs ?? [])
        manualExtraArgsArea.text = extractManualArgs(rawExtra).join("\n")

        let envText = "{}"
        try {
            envText = JSON.stringify(lp.envOverrides ?? {}, null, 2)
        } catch (e) {}
        envArea.text = envText

        const bp = App.profileManager.getBackend(backendId)
        backendNameCurrent = bp.name ?? ""
        backendHost.text = bp.host ?? "127.0.0.1"
        backendPort.text = (bp.port ?? 8080).toString()
        backendBinary.currentIndex = Math.max(0, backendBinary.indexOfValue(bp.binaryId ?? ""))

        const mp = App.profileManager.getModelProfile(modelProfileId)
        modelNameCurrent = mp.name ?? ""
        modelMain.currentIndex = Math.max(0, modelMain.indexOfValue(mp.modelId ?? ""))
        modelMmproj.currentIndex = Math.max(0, modelMmproj.indexOfValue(mp.mmprojId ?? ""))
        modelDraft.currentIndex = Math.max(0, modelDraft.indexOfValue(mp.draftModelId ?? ""))

        const rt = App.profileManager.getRuntimePreset(runtimeId)
        runtimeNameCurrent = rt.name ?? ""
        ctxField.text = (rt.ctx ?? 4096).toString()
        batchField.text = (rt.batch ?? 512).toString()
        ubatchField.text = (rt.ubatch ?? 512).toString()
        threadsField.text = (rt.threads ?? -1).toString()
        gpuLayersField.text = (rt.gpuLayers ?? -1).toString()
        parallelSlotsField.text = (rt.parallelSlots ?? 1).toString()
        cacheTypeField.text = rt.cacheType ?? "f16"
        flashAttnCheck.checked = rt.flashAttention ?? false
        mmapCheck.checked = rt.mmap ?? true
        mlockCheck.checked = rt.mlock ?? false
        contBatchCheck.checked = rt.contBatching ?? true

        aliasField.text = ""
        nPredictField.text = ""
        cacheTypeVField.text = ""
        tempField.text = ""
        topPField.text = ""
        topKField.text = ""
        repeatPenaltyField.text = ""
        presencePenaltyField.text = ""
        noContextShiftCheck.checked = false
        metricsCheck.checked = false
        noWarmupCheck.checked = false
        cacheRamField.text = ""
        cacheReuseField.text = ""
        for (let i = 0; i < rawExtra.length; ++i) {
            const cur = rawExtra[i]
            const nxt = (i + 1 < rawExtra.length) ? rawExtra[i + 1] : ""
            if (cur === "--alias") { aliasField.text = nxt; i += 1; continue }
            if (cur === "--n-predict") { nPredictField.text = nxt; i += 1; continue }
            if (cur === "--cache-type-v") { cacheTypeVField.text = nxt; i += 1; continue }
            if (cur === "--temp") { tempField.text = nxt; i += 1; continue }
            if (cur === "--top-p") { topPField.text = nxt; i += 1; continue }
            if (cur === "--top-k") { topKField.text = nxt; i += 1; continue }
            if (cur === "--repeat-penalty") { repeatPenaltyField.text = nxt; i += 1; continue }
            if (cur === "--presence-penalty") { presencePenaltyField.text = nxt; i += 1; continue }
            if (cur === "--cache-ram") { cacheRamField.text = nxt; i += 1; continue }
            if (cur === "--cache-reuse") { cacheReuseField.text = nxt; i += 1; continue }
            if (cur === "--no-context-shift") { noContextShiftCheck.checked = true; continue }
            if (cur === "--metrics") { metricsCheck.checked = true; continue }
            if (cur === "--no-warmup") { noWarmupCheck.checked = true; continue }
        }
    }

    function saveAll() {
        if (!selectedLaunchId || selectedLaunchId.length === 0) return

        let envOverrides = {}
        try {
            envOverrides = JSON.parse(envArea.text.length > 0 ? envArea.text : "{}")
        } catch (e) {
            App.serverError("envOverrides JSON inválido.")
            return
        }

        const bpOk = App.profileManager.updateBackend(
            backendId,
            backendNameCurrent,
            backendBinary.currentValue ?? "",
            backendHost.text,
            parseInt(backendPort.text),
            []
        )
        if (!bpOk) {
            App.serverError("No se pudo guardar Backend.")
            return
        }

        const mpOk = App.profileManager.updateModelProfile(
            modelProfileId,
            modelNameCurrent,
            modelMain.currentValue ?? "",
            modelMmproj.currentValue ?? "",
            modelDraft.currentValue ?? ""
        )
        if (!mpOk) {
            App.serverError("No se pudo guardar Model Profile.")
            return
        }

        const rtOk = App.profileManager.updateRuntimePreset({
            "id": runtimeId,
            "name": runtimeNameCurrent,
            "ctx": parseInt(ctxField.text),
            "batch": parseInt(batchField.text),
            "ubatch": parseInt(ubatchField.text),
            "threads": parseInt(threadsField.text),
            "gpuLayers": parseInt(gpuLayersField.text),
            "flashAttention": flashAttnCheck.checked,
            "mmap": mmapCheck.checked,
            "mlock": mlockCheck.checked,
            "contBatching": contBatchCheck.checked,
            "cacheType": cacheTypeField.text,
            "parallelSlots": parseInt(parallelSlotsField.text)
        })
        if (!rtOk) {
            App.serverError("No se pudo guardar Runtime.")
            return
        }

        const rebuiltArgs = []
        if (aliasField.text.trim().length > 0) rebuiltArgs.push("--alias", aliasField.text.trim())
        if (nPredictField.text.trim().length > 0) rebuiltArgs.push("--n-predict", nPredictField.text.trim())
        if (cacheTypeVField.text.trim().length > 0) rebuiltArgs.push("--cache-type-v", cacheTypeVField.text.trim())
        if (tempField.text.trim().length > 0) rebuiltArgs.push("--temp", tempField.text.trim())
        if (topPField.text.trim().length > 0) rebuiltArgs.push("--top-p", topPField.text.trim())
        if (topKField.text.trim().length > 0) rebuiltArgs.push("--top-k", topKField.text.trim())
        if (repeatPenaltyField.text.trim().length > 0) rebuiltArgs.push("--repeat-penalty", repeatPenaltyField.text.trim())
        if (presencePenaltyField.text.trim().length > 0) rebuiltArgs.push("--presence-penalty", presencePenaltyField.text.trim())
        if (noContextShiftCheck.checked) rebuiltArgs.push("--no-context-shift")
        if (metricsCheck.checked) rebuiltArgs.push("--metrics")
        if (noWarmupCheck.checked) rebuiltArgs.push("--no-warmup")
        if (cacheRamField.text.trim().length > 0) rebuiltArgs.push("--cache-ram", cacheRamField.text.trim())
        if (cacheReuseField.text.trim().length > 0) rebuiltArgs.push("--cache-reuse", cacheReuseField.text.trim())

        const manual = splitArgs(manualExtraArgsArea.text)
        for (let i = 0; i < manual.length; ++i) rebuiltArgs.push(manual[i])

        const lpOk = App.profileManager.updateLaunchProfile({
            "id": selectedLaunchId,
            "name": launchCombo.displayText,
            "backendProfileId": backendId,
            "modelProfileId": modelProfileId,
            "runtimePresetId": runtimeId,
            "extraArgs": rebuiltArgs,
            "envOverrides": envOverrides
        })
        if (!lpOk) {
            App.serverError("No se pudo guardar Launch Profile.")
            return
        }
    }

    Component.onCompleted: {
        if (App.profileManager.launchProfiles.rowCount() > 0) {
            const idx = App.profileManager.launchProfiles.index(0, 0)
            selectedLaunchId = App.profileManager.launchProfiles.data(idx, 257) ?? ""
            loadLaunch()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: "Profiles"
            subtitle: "Editor completo de Launch Profile"
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ColumnLayout {
                width: Math.max(900, root.width - 32)
                spacing: 12
                anchors.margins: 16

                Rectangle {
                    Layout.fillWidth: true
                    color: "#181825"
                    border.color: "#313244"
                    radius: 8
                    implicitHeight: topRow.implicitHeight + 20

                    RowLayout {
                        id: topRow
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        Text { text: "Launch Profile"; color: "#a6adc8"; font.pixelSize: 13 }
                        ComboBox {
                            id: launchCombo
                            Layout.fillWidth: true
                            model: App.profileManager.launchProfiles
                            textRole: "name"
                            valueRole: "profileId"
                            onCurrentValueChanged: {
                                selectedLaunchId = currentValue ?? ""
                                loadLaunch()
                            }
                            background: Rectangle { color: "#11111b"; radius: 6; border.color: "#313244" }
                            contentItem: Text {
                                text: launchCombo.displayText.length > 0 ? launchCombo.displayText : "— select —"
                                color: "#cdd6f4"; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter
                            }
                        }
                        LcButton { text: "Cancelar"; secondary: true; onClicked: loadLaunch() }
                        LcButton { text: "Guardar"; onClicked: saveAll() }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    color: "#181825"
                    border.color: "#313244"
                    radius: 8
                    implicitHeight: backendGrid.implicitHeight + 20
                    GridLayout {
                        id: backendGrid
                        anchors.fill: parent
                        anchors.margins: 10
                        columns: 2
                        rowSpacing: 8
                        columnSpacing: 10

                        Text { text: "Binary"; color: "#a6adc8"; font.pixelSize: 12 }
                        ComboBox {
                            id: backendBinary
                            Layout.fillWidth: true
                            model: App.binaryRegistry
                            textRole: "name"
                            valueRole: "binId"
                            background: Rectangle { color: "#11111b"; radius: 6; border.color: "#313244" }
                            contentItem: Text { text: backendBinary.displayText; color: "#cdd6f4"; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                        }
                        Text { text: "Host"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: backendHost; Layout.fillWidth: true }
                        Text { text: "Port"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: backendPort; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    color: "#181825"
                    border.color: "#313244"
                    radius: 8
                    implicitHeight: modelGrid.implicitHeight + 20
                    GridLayout {
                        id: modelGrid
                        anchors.fill: parent
                        anchors.margins: 10
                        columns: 2
                        rowSpacing: 8
                        columnSpacing: 10

                        Text { text: "Main model"; color: "#a6adc8"; font.pixelSize: 12 }
                        ComboBox {
                            id: modelMain
                            Layout.fillWidth: true
                            model: App.modelCatalog
                            textRole: "fileName"
                            valueRole: "modelId"
                            background: Rectangle { color: "#11111b"; radius: 6; border.color: "#313244" }
                            contentItem: Text { text: modelMain.displayText; color: "#cdd6f4"; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                        }
                        Text { text: "mmproj"; color: "#a6adc8"; font.pixelSize: 12 }
                        ComboBox {
                            id: modelMmproj
                            Layout.fillWidth: true
                            model: App.modelCatalog
                            textRole: "fileName"
                            valueRole: "modelId"
                            background: Rectangle { color: "#11111b"; radius: 6; border.color: "#313244" }
                            contentItem: Text { text: modelMmproj.displayText; color: "#cdd6f4"; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                        }
                        Text { text: "Draft model"; color: "#a6adc8"; font.pixelSize: 12 }
                        ComboBox {
                            id: modelDraft
                            Layout.fillWidth: true
                            model: App.modelCatalog
                            textRole: "fileName"
                            valueRole: "modelId"
                            background: Rectangle { color: "#11111b"; radius: 6; border.color: "#313244" }
                            contentItem: Text { text: modelDraft.displayText; color: "#cdd6f4"; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    color: "#181825"
                    border.color: "#313244"
                    radius: 8
                    implicitHeight: runtimeGrid.implicitHeight + 20
                    GridLayout {
                        id: runtimeGrid
                        anchors.fill: parent
                        anchors.margins: 10
                        columns: 4
                        rowSpacing: 8
                        columnSpacing: 10

                        Text { text: "ctx"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: ctxField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }

                        Text { text: "batch"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: batchField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "ubatch"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: ubatchField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }

                        Text { text: "threads"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: threadsField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "gpuLayers"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: gpuLayersField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }

                        Text { text: "parallelSlots"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: parallelSlotsField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "cacheType"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: cacheTypeField; Layout.fillWidth: true }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    CheckBox { id: flashAttnCheck; text: "flash-attn"; contentItem: Text { text: parent.text; color: "#a6adc8"; leftPadding: parent.indicator.width + 6 } }
                    CheckBox { id: mmapCheck; text: "mmap"; contentItem: Text { text: parent.text; color: "#a6adc8"; leftPadding: parent.indicator.width + 6 } }
                    CheckBox { id: mlockCheck; text: "mlock"; contentItem: Text { text: parent.text; color: "#a6adc8"; leftPadding: parent.indicator.width + 6 } }
                    CheckBox { id: contBatchCheck; text: "cont-batching"; contentItem: Text { text: parent.text; color: "#a6adc8"; leftPadding: parent.indicator.width + 6 } }
                }

                Rectangle {
                    Layout.fillWidth: true
                    color: "#181825"
                    border.color: "#313244"
                    radius: 8
                    implicitHeight: advancedGrid.implicitHeight + 20
                    GridLayout {
                        id: advancedGrid
                        anchors.fill: parent
                        anchors.margins: 10
                        columns: 4
                        rowSpacing: 8
                        columnSpacing: 10

                        Text { text: "alias"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: aliasField; Layout.fillWidth: true }
                        Text { text: "n-predict"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: nPredictField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }

                        Text { text: "cache-type-v"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: cacheTypeVField; Layout.fillWidth: true }
                        Text { text: "temp"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: tempField; Layout.fillWidth: true }

                        Text { text: "top-p"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: topPField; Layout.fillWidth: true }
                        Text { text: "top-k"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: topKField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }

                        Text { text: "repeat-penalty"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: repeatPenaltyField; Layout.fillWidth: true }
                        Text { text: "presence-penalty"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: presencePenaltyField; Layout.fillWidth: true }

                        Text { text: "cache-ram"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: cacheRamField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "cache-reuse"; color: "#a6adc8"; font.pixelSize: 12 }
                        LcTextField { id: cacheReuseField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    CheckBox { id: noContextShiftCheck; text: "no-context-shift"; contentItem: Text { text: parent.text; color: "#a6adc8"; leftPadding: parent.indicator.width + 6 } }
                    CheckBox { id: metricsCheck; text: "metrics"; contentItem: Text { text: parent.text; color: "#a6adc8"; leftPadding: parent.indicator.width + 6 } }
                    CheckBox { id: noWarmupCheck; text: "no-warmup"; contentItem: Text { text: parent.text; color: "#a6adc8"; leftPadding: parent.indicator.width + 6 } }
                }

                Rectangle {
                    Layout.fillWidth: true
                    color: "#181825"
                    border.color: "#313244"
                    radius: 8
                    implicitHeight: 220
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        Text { text: "Extra args manuales (se agregan al final)"; color: "#a6adc8"; font.pixelSize: 12 }
                        TextArea {
                            id: manualExtraArgsArea
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: "#cdd6f4"
                            background: Rectangle { color: "#11111b"; radius: 6; border.color: "#313244" }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    color: "#181825"
                    border.color: "#313244"
                    radius: 8
                    implicitHeight: 220
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        Text { text: "envOverrides (JSON)"; color: "#a6adc8"; font.pixelSize: 12 }
                        TextArea {
                            id: envArea
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: "#cdd6f4"
                            font.family: "Consolas"
                            background: Rectangle { color: "#11111b"; radius: 6; border.color: "#313244" }
                        }
                    }
                }

                Item { Layout.fillHeight: true; Layout.preferredHeight: 16 }
            }
        }
    }
}
