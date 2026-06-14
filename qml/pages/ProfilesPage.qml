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
    property bool mmprojEnabled: false
    property bool draftEnabled: false
    property bool launchFavorite: false   // favorito del perfil de lanzamiento
    property bool smokeTestRunning: false
    property string harnessAdapter: "none"
    property string harnessProfileId: ""

    // ── Maestro (supervisor) ──────────────────────────────────────
    property string masterKind: "none"          // none | http | cli
    property string masterCliName: "claude"
    property string masterEscalation: "manual"  // manual | auto | both
    property int    masterAutoAfterFails: 3
    property bool   masterApplyEdits: true
    property var    masterCliStatusCache: ({})   // name -> status map
    function refreshMasterCliStatus(force) {
        const names = App.masterCliList()
        let m = {}
        for (let i = 0; i < names.length; ++i)
            m[names[i]] = App.masterCliStatus(names[i], force === true)
        masterCliStatusCache = m
    }

    function splitArgs(raw) {
        const out = []
        const lines = raw.split("\n")
        for (let i = 0; i < lines.length; ++i) {
            const tokens = lines[i].trim().split(/\s+/).filter(s => s.length > 0)
            for (let j = 0; j < tokens.length; ++j) out.push(tokens[j])
        }
        return out
    }

    function argsFlat() { return splitArgs(extraArgsArea.text) }

    // ¿El token es un flag (-x / --xxx) y NO un valor numérico negativo (-1, -0.5)?
    function isArgFlag(tok) {
        return /^-/.test(tok) && !/^-?\d*\.?\d+$/.test(tok)
    }

    // Agrupa flag+valor en una línea: "-np 1", "--reasoning off". Los flags sin
    // valor (seguidos de otro flag) quedan solos en su línea.
    function formatArgsForDisplay(tokens) {
        const lines = []
        for (let i = 0; i < tokens.length; ++i) {
            const t = tokens[i]
            if (isArgFlag(t) && i + 1 < tokens.length && !isArgFlag(tokens[i + 1])) {
                lines.push(t + " " + tokens[i + 1])
                ++i
            } else {
                lines.push(t)
            }
        }
        return lines.join("\n")
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
        const boolFlags = { "--no-context-shift": true, "--context-shift": true, "--metrics": true, "--no-warmup": true }
        const out = []
        for (let i = 0; i < rawArgs.length; ++i) {
            const cur = rawArgs[i]
            if (pairFlags[cur]) { i += 1; continue }
            if (boolFlags[cur]) continue
            out.push(cur)
        }
        return out
    }

    function selectProfile(id) {
        if (!id || id.length === 0) return false
        const count = App.profileManager.launchProfiles.rowCount()
        let found = false
        for (let i = 0; i < count; ++i) {
            const idx = App.profileManager.launchProfiles.index(i, 0)
            const pid = App.profileManager.launchProfiles.data(idx, 257) ?? ""
            if (pid === id) { launchCombo.currentIndex = i; found = true; break }
        }
        if (!found) return false
        selectedLaunchId = id
        loadLaunch()
        return true
    }

    function newProfile() {
        const bId = App.profileManager.addBackend("Backend", "", "127.0.0.1", 8080)
        const mId = App.profileManager.addModelProfile("Model", "", "", "")
        const rId = App.profileManager.addRuntimePreset("Runtime", 4096, 512, -1, false, true)
        const lId = App.profileManager.addLaunchProfile("Nuevo perfil", bId, mId, rId)
        selectProfile(lId)
    }

    function tokenizeArgs(raw) {
        // Join continuation lines (\<newline>), then tokenize respecting quotes
        const cleaned = raw.replace(/\\\s*\n/g, ' ').replace(/\n/g, ' ')
        const tokens = []; let cur = ''; let inQ = false; let qch = ''
        for (let i = 0; i < cleaned.length; i++) {
            const ch = cleaned[i]
            if ((ch === '"' || ch === "'") && !inQ) { inQ = true; qch = ch }
            else if (ch === qch && inQ)             { inQ = false }
            else if (ch === ' ' && !inQ)            { if (cur.length) { tokens.push(cur); cur = '' } }
            else                                    { cur += ch }
        }
        if (cur.length) tokens.push(cur)
        return tokens
    }

    function importFromArgs(profileName, rawArgs) {
        const tokens = tokenizeArgs(rawArgs)
        // Skip binary / script name (first token not starting with -)
        let i = (tokens.length > 0 && !tokens[0].startsWith('-')) ? 1 : 0

        let host = '127.0.0.1', port = 8080, modelPath = ''
        let ctx = 4096, batch = 512, ubatch = 512, threads = -1, gpuLayers = -1
        let flashAttn = false, useMmap = true, useMlock = false, contBatch = true
        let parallel = 1, cacheType = 'f16'
        const extra = []

        while (i < tokens.length) {
            const t = tokens[i]
            const next = tokens[i + 1] ?? ''
            // Pair flags
            if ((t === '--host') && next)                         { host = next;               i += 2 }
            else if ((t === '--port') && next)                    { port = parseInt(next)||8080; i += 2 }
            else if ((t === '--model' || t === '-m') && next)     { modelPath = next;           i += 2 }
            else if ((t === '--ctx-size' || t === '-c' || t === '--ctx') && next)
                                                                  { ctx = parseInt(next)||4096; i += 2 }
            else if (t === '--batch-size' && next)                { batch = parseInt(next)||512; i += 2 }
            else if (t === '--ubatch-size' && next)               { ubatch = parseInt(next)||512; i += 2 }
            else if ((t === '--threads' || t === '-t') && next)   { threads = parseInt(next)||-1; i += 2 }
            else if ((t === '--n-gpu-layers' || t === '-ngl' || t === '--n_gpu_layers') && next)
                                                                  { gpuLayers = parseInt(next); i += 2 }
            else if ((t === '--parallel' || t === '-np') && next) { parallel = parseInt(next)||1; i += 2 }
            else if (t === '--cache-type-k' && next)              { cacheType = next;            i += 2 }
            // Bool flags
            else if (t === '--flash-attn' || t === '-fa')         { flashAttn = true;  i++ }
            else if (t === '--no-mmap')                           { useMmap   = false; i++ }
            else if (t === '--mlock')                             { useMlock  = true;  i++ }
            else if (t === '--cont-batching' || t === '-cb')      { contBatch = true;  i++ }
            // Unknown → extra
            else                                                  { extra.push(t); i++ }
        }

        const bId = App.profileManager.addBackend(profileName + " · Backend", "", host, port)
        const mId = App.profileManager.addModelProfile(profileName + " · Model", "", "", "")
        const rId = App.profileManager.addRuntimePreset(profileName + " · Runtime", ctx, batch, gpuLayers, flashAttn, contBatch)
        App.profileManager.updateRuntimePreset({
            "id": rId, "name": profileName + " · Runtime",
            "ctx": ctx, "batch": batch, "ubatch": ubatch,
            "threads": threads, "gpuLayers": gpuLayers,
            "flashAttention": flashAttn, "mmap": useMmap, "mlock": useMlock,
            "contBatching": contBatch, "cacheType": cacheType, "parallelSlots": parallel
        })
        const lId = App.profileManager.addLaunchProfile(profileName, bId, mId, rId)
        // Put unparsed args + model path as extraArgs if needed
        const extras = extra.slice()
        if (modelPath.length > 0) extras.unshift(modelPath, '--model')
        if (extras.length > 0)
            App.profileManager.updateLaunchProfile({
                "id": lId, "name": profileName,
                "backendProfileId": bId, "modelProfileId": mId, "runtimePresetId": rId,
                "extraArgs": extras, "envOverrides": {}
            })
        selectProfile(lId)
    }

    function duplicateProfile() {
        if (!selectedLaunchId || selectedLaunchId.length === 0) return
        const lp = App.profileManager.getLaunchProfile(selectedLaunchId)
        const bp = App.profileManager.getBackend(lp.backendProfileId ?? "")
        const mp = App.profileManager.getModelProfile(lp.modelProfileId ?? "")
        const rt = App.profileManager.getRuntimePreset(lp.runtimePresetId ?? "")

        const bId = App.profileManager.addBackend(bp.name ?? "Backend", bp.binaryId ?? "", bp.host ?? "127.0.0.1", bp.port ?? 8080)
        const mId = App.profileManager.addModelProfile(mp.name ?? "Model", mp.modelId ?? "", mp.mmprojId ?? "", mp.draftModelId ?? "")
        const rId = App.profileManager.addRuntimePreset(rt.name ?? "Runtime", rt.ctx ?? 4096, rt.batch ?? 512, rt.gpuLayers ?? -1, rt.flashAttention ?? false, rt.contBatching ?? true)
        App.profileManager.updateRuntimePreset({
            "id": rId, "name": rt.name ?? "Runtime",
            "ctx": rt.ctx ?? 4096, "batch": rt.batch ?? 512, "ubatch": rt.ubatch ?? 512,
            "threads": rt.threads ?? -1, "gpuLayers": rt.gpuLayers ?? -1,
            "flashAttention": rt.flashAttention ?? false, "mmap": rt.mmap ?? true,
            "mlock": rt.mlock ?? false, "contBatching": rt.contBatching ?? true,
            "cacheType": rt.cacheType ?? "f16", "parallelSlots": rt.parallelSlots ?? 1
        })

        const lId = App.profileManager.addLaunchProfile((lp.name ?? "Perfil") + " (copia)", bId, mId, rId)
        App.profileManager.updateLaunchProfile({
            "id": lId, "name": (lp.name ?? "Perfil") + " (copia)",
            "backendProfileId": bId, "modelProfileId": mId, "runtimePresetId": rId,
            "extraArgs": lp.extraArgs ?? [], "envOverrides": lp.envOverrides ?? {}
        })
        selectProfile(lId)
    }

    function loadLaunch() {
        if (!selectedLaunchId || selectedLaunchId.length === 0) return
        const lp = App.profileManager.getLaunchProfile(selectedLaunchId)
        if (!lp || !lp.id) return

        backendId = lp.backendProfileId ?? ""
        modelProfileId = lp.modelProfileId ?? ""
        runtimeId = lp.runtimePresetId ?? ""
        harnessProfileId = lp.harnessProfileId ?? ""
        if (harnessProfileId.length > 0) {
            const hp = App.profileManager.getHarness(harnessProfileId)
            harnessAdapter = hp.adapter ?? "none"
        } else {
            harnessAdapter = "none"
        }
        const rawExtra = (lp.extraArgs ?? [])
        manualExtraArgsArea.text = formatArgsForDisplay(extractManualArgs(rawExtra))

        // Alias (display) opcional + favorito del perfil.
        root.launchFavorite = (lp.favorite === true)
        profileAliasField.text = lp.alias ?? ""

        const bp = App.profileManager.getBackend(backendId)
        backendNameCurrent = bp.name ?? ""
        backendHost.text = bp.host ?? "127.0.0.1"
        backendPort.text = (bp.port ?? 8080).toString()
        backendBinary.currentIndex = Math.max(0, backendBinary.indexOfValue(bp.binaryId ?? ""))

        const mp = App.profileManager.getModelProfile(modelProfileId)
        modelNameCurrent = mp.name ?? ""
        modelMain.currentIndex = Math.max(0, modelMain.indexOfValue(mp.modelId ?? ""))
        mmprojEnabled = (mp.mmprojId ?? "").length > 0
        modelMmproj.currentIndex = Math.max(0, modelMmproj.indexOfValue(mp.mmprojId ?? ""))
        draftEnabled = (mp.draftModelId ?? "").length > 0
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

        aliasField.text = ""; nPredictField.text = ""; cacheTypeVField.text = ""
        tempField.text = ""; topPField.text = ""; topKField.text = ""
        repeatPenaltyField.text = ""; presencePenaltyField.text = ""
        contextShiftCheck.checked = true
        metricsCheck.checked = false; noWarmupCheck.checked = false
        cacheRamField.text = ""; cacheReuseField.text = ""
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
            if (cur === "--no-context-shift") { contextShiftCheck.checked = false; continue }
            if (cur === "--context-shift")    { contextShiftCheck.checked = true;  continue }
            if (cur === "--metrics") { metricsCheck.checked = true; continue }
            if (cur === "--no-warmup") { noWarmupCheck.checked = true; continue }
        }

        // Maestro (supervisor)
        const mc = lp.master ?? {}
        masterKind = mc.kind ?? "none"
        masterCliName = (mc.cliName && mc.cliName.length > 0) ? mc.cliName : "claude"
        masterEscalation = mc.escalation ?? "manual"
        masterAutoAfterFails = mc.autoAfterFails ?? 3
        masterApplyEdits = (mc.applyEdits !== false)
        masterHttpUrlField.text = mc.httpUrl ?? ""
        masterHttpModelField.text = mc.httpModel ?? ""
        masterHttpKeyField.text = mc.httpKey ?? ""
        refreshMasterCliStatus(false)

        // Refresca vista previa del comando para el perfil cargado
        App.computeEffectiveProfile(selectedLaunchId)
    }

    // Arma los args extra (manuales + campos) tal como van al final del comando.
    function collectExtraArgs(binId) {
        function rf(flag) { return binId.length > 0 ? App.resolveFlag(binId, flag) : flag }
        const out = []
        if (aliasField.text.trim().length > 0) out.push(rf("--alias"), aliasField.text.trim())
        if (nPredictField.text.trim().length > 0) out.push(rf("--n-predict"), nPredictField.text.trim())
        if (cacheTypeVField.text.trim().length > 0) out.push(rf("--cache-type-v"), cacheTypeVField.text.trim())
        if (tempField.text.trim().length > 0) out.push(rf("--temp"), tempField.text.trim())
        if (topPField.text.trim().length > 0) out.push(rf("--top-p"), topPField.text.trim())
        if (topKField.text.trim().length > 0) out.push(rf("--top-k"), topKField.text.trim())
        if (repeatPenaltyField.text.trim().length > 0) out.push(rf("--repeat-penalty"), repeatPenaltyField.text.trim())
        if (presencePenaltyField.text.trim().length > 0) out.push(rf("--presence-penalty"), presencePenaltyField.text.trim())
        // Never use rf() for these — avoids alias mapping --no-context-shift → --context-shift
        out.push(contextShiftCheck.checked ? "--context-shift" : "--no-context-shift")
        if (metricsCheck.checked)  out.push("--metrics")
        if (noWarmupCheck.checked) out.push("--no-warmup")
        if (cacheRamField.text.trim().length > 0) out.push(rf("--cache-ram"), cacheRamField.text.trim())
        if (cacheReuseField.text.trim().length > 0) out.push(rf("--cache-reuse"), cacheReuseField.text.trim())
        const manual = splitArgs(manualExtraArgsArea.text)
        for (let i = 0; i < manual.length; ++i)
            out.push(manual[i].startsWith('-') ? rf(manual[i]) : manual[i])
        return out
    }

    // Recalcula la vista previa del comando con los valores actuales del editor (sin guardar).
    function recomputePreview() {
        if (!selectedLaunchId || selectedLaunchId.length === 0) return
        const binId = backendBinary.currentValue ?? ""
        App.computeEffectiveProfilePreview(selectedLaunchId, {
            "host": backendHost.text,
            "port": parseInt(backendPort.text) || 8080,
            "binaryId": binId,
            "modelId": modelMain.currentValue ?? "",
            "mmprojId": mmprojEnabled ? (modelMmproj.currentValue ?? "") : "",
            "draftModelId": draftEnabled ? (modelDraft.currentValue ?? "") : "",
            "ctx": parseInt(ctxField.text) || 4096,
            "batch": parseInt(batchField.text) || 512,
            "ubatch": parseInt(ubatchField.text) || 512,
            "threads": parseInt(threadsField.text) || -1,
            "gpuLayers": parseInt(gpuLayersField.text) || -1,
            "flashAttention": flashAttnCheck.checked,
            "mmap": mmapCheck.checked,
            "mlock": mlockCheck.checked,
            "contBatching": contBatchCheck.checked,
            "cacheType": cacheTypeField.text,
            "parallelSlots": parseInt(parallelSlotsField.text) || 1,
            "extraArgs": collectExtraArgs(binId)
        })
    }

    function saveAll() {
        if (!selectedLaunchId || selectedLaunchId.length === 0) return

        // envOverrides ya no se edita en UI; se preserva el valor existente del perfil.
        let envOverrides = {}
        try {
            const lpCur = App.profileManager.getLaunchProfile(selectedLaunchId)
            envOverrides = lpCur.envOverrides ?? {}
        } catch (e) { envOverrides = {} }

        // Backend: update if exists, create if not
        const binaryId = backendBinary.currentValue ?? ""
        let effectiveBid = backendId
        if (!App.profileManager.updateBackend(effectiveBid, backendNameCurrent, binaryId, backendHost.text, parseInt(backendPort.text), [])) {
            effectiveBid = App.profileManager.addBackend(
                backendNameCurrent.length > 0 ? backendNameCurrent : "Backend",
                binaryId, backendHost.text, parseInt(backendPort.text))
            if (!effectiveBid || effectiveBid.length === 0) { App.serverError("No se pudo crear Backend."); return }
            backendId = effectiveBid
        }

        // Model: update if exists, create if not
        const mainModelId  = modelMain.currentValue  ?? ""
        const mmprojId     = mmprojEnabled  ? (modelMmproj.currentValue ?? "") : ""
        const draftModelId = draftEnabled   ? (modelDraft.currentValue  ?? "") : ""
        let effectiveMid = modelProfileId
        if (!App.profileManager.updateModelProfile(effectiveMid, modelNameCurrent, mainModelId, mmprojId, draftModelId)) {
            effectiveMid = App.profileManager.addModelProfile(
                modelNameCurrent.length > 0 ? modelNameCurrent : "Model",
                mainModelId, mmprojId, draftModelId)
            if (!effectiveMid || effectiveMid.length === 0) { App.serverError("No se pudo crear Model Profile."); return }
            modelProfileId = effectiveMid
        }

        // Runtime: update if exists, create if not
        let effectiveRid = runtimeId
        const rtData = {
            "id": effectiveRid, "name": runtimeNameCurrent,
            "ctx": parseInt(ctxField.text), "batch": parseInt(batchField.text),
            "ubatch": parseInt(ubatchField.text), "threads": parseInt(threadsField.text),
            "gpuLayers": parseInt(gpuLayersField.text), "flashAttention": flashAttnCheck.checked,
            "mmap": mmapCheck.checked, "mlock": mlockCheck.checked,
            "contBatching": contBatchCheck.checked, "cacheType": cacheTypeField.text,
            "parallelSlots": parseInt(parallelSlotsField.text)
        }
        if (!App.profileManager.updateRuntimePreset(rtData)) {
            effectiveRid = App.profileManager.addRuntimePreset(
                runtimeNameCurrent.length > 0 ? runtimeNameCurrent : "Runtime",
                parseInt(ctxField.text), parseInt(batchField.text),
                parseInt(gpuLayersField.text), flashAttnCheck.checked, contBatchCheck.checked)
            if (!effectiveRid || effectiveRid.length === 0) { App.serverError("No se pudo crear Runtime."); return }
            runtimeId = effectiveRid
            App.profileManager.updateRuntimePreset(Object.assign({}, rtData, {"id": effectiveRid}))
        }

        const bp = App.profileManager.getBackend(backendId)
        const binId = bp.binaryId ?? ""
        const rebuiltArgs = collectExtraArgs(binId)

        // Persist harness selection
        let resolvedHarnessId = ""
        if (harnessAdapter !== "none") {
            if (harnessProfileId.length > 0) {
                App.profileManager.updateHarness({"id": harnessProfileId, "adapter": harnessAdapter, "name": harnessAdapter})
                resolvedHarnessId = harnessProfileId
            } else {
                resolvedHarnessId = App.profileManager.addHarness(harnessAdapter, harnessAdapter)
                harnessProfileId = resolvedHarnessId
            }
        }

        console.log("[saveAll] selectedLaunchId=", selectedLaunchId,
                    "bid=", effectiveBid, "mid=", effectiveMid, "rid=", effectiveRid,
                    "args=", rebuiltArgs.length)

        const lpOk = App.profileManager.updateLaunchProfile({
            "id": selectedLaunchId, "name": launchCombo.displayText,
            "alias": profileAliasField.text.trim(), "favorite": root.launchFavorite,
            "backendProfileId": effectiveBid, "modelProfileId": effectiveMid,
            "runtimePresetId": effectiveRid, "extraArgs": rebuiltArgs, "envOverrides": envOverrides,
            "harnessProfileId": resolvedHarnessId,
            "master": {
                "kind": masterKind, "cliName": masterCliName,
                "httpUrl": masterHttpUrlField.text.trim(),
                "httpModel": masterHttpModelField.text.trim(),
                "httpKey": masterHttpKeyField.text.trim(),
                "escalation": masterEscalation,
                "autoAfterFails": masterAutoAfterFails,
                "applyEdits": masterApplyEdits
            }
        })

        console.log("[saveAll] updateLaunchProfile result:", lpOk)
        App.profileManager.saveProfiles()   // fuerza write a disco

        if (!lpOk) App.serverError("No se pudo guardar Launch Profile.")

        // Recalcula vista previa con los args recién guardados
        App.computeEffectiveProfile(selectedLaunchId)
    }

    Component.onCompleted: {
        const lastId = App.readSetting("lastLaunchId", "")
        if (lastId === "" || !selectProfile(lastId)) {
            if (App.profileManager.launchProfiles.rowCount() > 0) {
                const idx = App.profileManager.launchProfiles.index(0, 0)
                selectedLaunchId = App.profileManager.launchProfiles.data(idx, 257) ?? ""
                loadLaunch()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: (App.langV, App.l("profiles.title"))
            subtitle: (App.langV, App.l("profiles.subtitle"))
        }

        ScrollView {
            id: editorScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            leftPadding: 16
            rightPadding: 16
            topPadding: 16
            bottomPadding: 16
            contentWidth: Math.max(900, editorScroll.availableWidth)

            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: LcScrollBar {
                parent: editorScroll
                anchors.right: editorScroll.right
                anchors.top: editorScroll.top
                anchors.bottom: editorScroll.bottom
                anchors.topMargin: editorScroll.topPadding
                anchors.bottomMargin: editorScroll.bottomPadding
                policy: ScrollBar.AsNeeded
            }

            Item {
                width: Math.max(900, editorScroll.availableWidth)
                implicitHeight: editorCol.implicitHeight

                ColumnLayout {
                id: editorCol
                anchors { left: parent.left; right: parent.right; top: parent.top }
                spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    radius: 8
                    implicitHeight: topRow.implicitHeight + 20

                    RowLayout {
                        id: topRow
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        Text { text: (App.langV, App.l("launch.profile")); color: Theme.textSecondary; font.pixelSize: 13 }
                        LcComboBox {
                            id: launchCombo
                            Layout.fillWidth: true
                            model: App.profileManager.launchProfiles
                            textRole: "name"
                            valueRole: "profileId"
                            onCurrentValueChanged: { selectedLaunchId = currentValue ?? ""; loadLaunch() }
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text {
                                text: launchCombo.displayText.length > 0 ? launchCombo.displayText : (App.langV, App.l("common.selectPlaceholder"))
                                color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter
                            }
                        }
                        // Favorito (★): se guarda al instante y manda el perfil arriba del dropdown.
                        LcButton {
                            text: root.launchFavorite ? "★" : "☆"
                            secondary: true
                            enabled: selectedLaunchId.length > 0
                            ToolTip.visible: hovered
                            ToolTip.text: root.launchFavorite ? "Quitar de favoritos" : "Marcar favorito"
                            onClicked: {
                                root.launchFavorite = !root.launchFavorite
                                App.profileManager.setLaunchFavorite(selectedLaunchId, root.launchFavorite)
                            }
                        }
                        // Alias opcional (prioridad sobre el nombre en los dropdowns).
                        LcTextField {
                            id: profileAliasField
                            Layout.preferredWidth: 160
                            placeholderText: "Alias (opcional)"
                            enabled: selectedLaunchId.length > 0
                            onEditingFinished: if (selectedLaunchId.length > 0)
                                App.profileManager.setLaunchAlias(selectedLaunchId, text.trim())
                        }
                        LcButton {
                            text: {
                                const _lang = App.langV
                                return smokeTestRunning ? App.l("profiles.smokeTesting") : App.l("profiles.smokeTest")
                            }
                            secondary: true
                            enabled: selectedLaunchId.length > 0 && !smokeTestRunning && !App.serverRunning
                            onClicked: { saveAll(); smokeTestRunning = true; App.smokeTestServer(selectedLaunchId) }
                        }
                        LcButton {
                            text: App.autoTuneRunning ? "Tuning…" : "Auto-tune"
                            secondary: true
                            enabled: selectedLaunchId.length > 0 && !App.serverRunning && !App.autoTuneRunning
                            ToolTip.visible: hovered
                            ToolTip.text: "Optimiza ngl/batch/flash-attn/cache-type maximizando tok/s sin degradar calidad (gate). Crea un perfil nuevo \"-tuned\", no toca este"
                            onClicked: { saveAll(); App.startAutoTune(selectedLaunchId, 24, 0.6, 256) }
                        }
                        LcButton {
                            text: "Cancelar tune"
                            secondary: true
                            visible: App.autoTuneRunning
                            onClicked: App.cancelAutoTune()
                        }
                        LcButton { text: (App.langV, App.l("profiles.new")); secondary: true; onClicked: newProfile() }
                        LcButton { text: "Importar"; secondary: true; onClicked: importDialog.open() }
                        LcButton {
                            text: (App.langV, App.l("profiles.duplicate")); secondary: true
                            enabled: selectedLaunchId.length > 0
                            onClicked: duplicateProfile()
                        }
                        LcButton {
                            text: (App.langV, App.l("profiles.rename")); secondary: true
                            enabled: selectedLaunchId.length > 0
                            onClicked: { renameField.text = launchCombo.displayText; renameDialog.open() }
                        }
                        LcButton { text: (App.langV, App.l("profiles.cancel")); secondary: true; onClicked: loadLaunch() }
                        LcButton { text: (App.langV, App.l("profiles.save")); onClicked: saveAll() }
                        LcButton {
                            text: (App.langV, App.l("profiles.delete"))
                            danger: true
                            enabled: selectedLaunchId.length > 0
                            onClicked: deleteDialog.open()
                        }
                    }
                }

                // Estado del auto-tune (progreso + mejor config aplicada).
                Text {
                    visible: App.autoTuneRunning || App.autoTuneStatus.length > 0
                    Layout.fillWidth: true
                    text: (App.autoTuneRunning ? "⏳ " : "") + App.autoTuneStatus
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                LcDialog {
                    id: deleteDialog
                    title: (App.langV, App.l("profiles.deleteTitle"))
                    width: 440
                    height: 190
                    contentItem: Text {
                        width: 404
                        text: (App.langV, App.lf("common.deleteConfirm", launchCombo.displayText))
                        color: Theme.textPrimary
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                    onAccepted: {
                        const idToDelete = selectedLaunchId
                        App.profileManager.removeLaunchProfile(idToDelete)
                        selectedLaunchId = ""
                        if (App.profileManager.launchProfiles.rowCount() > 0) {
                            const idx = App.profileManager.launchProfiles.index(0, 0)
                            selectedLaunchId = App.profileManager.launchProfiles.data(idx, 257) ?? ""
                            launchCombo.currentIndex = 0
                            loadLaunch()
                        }
                    }
                }

                LcDialog {
                    id: smokeTestResultDialog
                    property bool passed: false
                    property string output: ""
                    title: {
                        const _lang = App.langV
                        return passed ? App.l("profiles.smokeTestPassed") : App.l("profiles.smokeTestFailed")
                    }
                    width: 520
                    height: 320
                    contentItem: Item {
                        width: 484
                        height: 220
                        Rectangle {
                            anchors.fill: parent
                            color: Theme.inputBg
                            radius: 6
                            border.color: smokeTestResultDialog.passed ? Theme.successText : Theme.errorText
                            clip: true
                            ScrollView {
                                anchors.fill: parent
                                anchors.margins: 8
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                TextArea {
                                    readOnly: true
                                    text: smokeTestResultDialog.output
                                    color: smokeTestResultDialog.passed ? Theme.successText : Theme.errorText
                                    font { family: "Consolas,monospace"; pixelSize: 11 }
                                    wrapMode: TextArea.WrapAnywhere
                                    background: null
                                }
                            }
                        }
                    }
                }

                Connections {
                    target: App
                    function onSmokeTestFinished(passed, output) {
                        smokeTestRunning = false
                        smokeTestResultDialog.passed = passed
                        smokeTestResultDialog.output = output
                        smokeTestResultDialog.open()
                    }
                }

                LcDialog {
                    id: renameDialog
                    title: (App.langV, App.l("profiles.renameTitle"))
                    width: 440
                    height: 200
                    contentItem: Item {
                        width: 404
                        height: 40
                        LcTextField {
                            id: renameField
                            anchors.fill: parent
                            Keys.onReturnPressed: renameDialog.accept()
                            Keys.onEnterPressed: renameDialog.accept()
                        }
                    }
                    onAccepted: {
                        const newName = renameField.text.trim()
                        if (newName.length === 0) return
                        const lp = App.profileManager.getLaunchProfile(selectedLaunchId)
                        App.profileManager.updateLaunchProfile({
                            "id": selectedLaunchId, "name": newName,
                            "backendProfileId": lp.backendProfileId ?? "",
                            "modelProfileId": lp.modelProfileId ?? "",
                            "runtimePresetId": lp.runtimePresetId ?? "",
                            "extraArgs": lp.extraArgs ?? [],
                            "envOverrides": lp.envOverrides ?? {}
                        })
                    }
                }

                // ── Import dialog ─────────────────────────────────────────────
                Dialog {
                    id: importDialog
                    title: "Importar perfil desde argumentos"
                    modal: true
                    parent: Overlay.overlay
                    x: Math.round((parent.width - width) / 2)
                    y: Math.round((parent.height - height) / 2)
                    width: 560
                    height: 420
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
                        Text {
                            anchors { left: parent.left; leftMargin: 22; verticalCenter: parent.verticalCenter }
                            text: "Importar perfil desde argumentos"
                            font { pixelSize: 14; bold: true }
                            color: Theme.textPrimary
                        }
                    }

                    footer: Rectangle {
                        color: Theme.popupHeaderBg; height: 56; radius: 12
                        Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
                        Rectangle { anchors.top: parent.top; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
                        Row {
                            anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                            spacing: 10
                            LcButton {
                                text: "Cancelar"; secondary: true
                                onClicked: { importDialog.close(); importNameField.text = ""; importArgsArea.text = "" }
                            }
                            LcButton {
                                text: "Importar"
                                enabled: importNameField.text.trim().length > 0 && importArgsArea.text.trim().length > 0
                                onClicked: {
                                    importFromArgs(importNameField.text.trim(), importArgsArea.text)
                                    importDialog.close()
                                    importNameField.text = ""
                                    importArgsArea.text = ""
                                }
                            }
                        }
                    }

                    contentItem: ColumnLayout {
                        spacing: 12
                        width: 520

                        Text { text: "Nombre del perfil:"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField {
                            id: importNameField
                            Layout.fillWidth: true
                            placeholderText: "Mi servidor local"
                            Keys.onTabPressed: importArgsArea.forceActiveFocus()
                        }

                        Text {
                            text: "Pegar argumentos (desde terminal, script, o README):"
                            color: Theme.textSecondary; font.pixelSize: 12
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: Theme.inputBg; radius: 8
                            border.color: importArgsArea.activeFocus ? Theme.inputBorderFocus : Theme.borderColor
                            border.width: 1
                            clip: true

                            ScrollView {
                                anchors.fill: parent; anchors.margins: 2
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                TextArea {
                                    id: importArgsArea
                                    placeholderText: "./llama-server --model /ruta/al/modelo.gguf --ctx-size 8192 --port 8080 --n-gpu-layers 99 --flash-attn"
                                    color: Theme.textPrimary
                                    placeholderTextColor: Theme.textMuted
                                    font { family: "Consolas,monospace"; pixelSize: 12 }
                                    wrapMode: TextArea.WrapAnywhere
                                    background: null
                                    padding: 10
                                    selectByMouse: true
                                }
                            }
                        }

                        Text {
                            text: "Se parsearán: --host --port --model --ctx-size --batch-size --ubatch-size --threads --n-gpu-layers --flash-attn --no-mmap --mlock --parallel --cache-type-k"
                            color: Theme.textMuted; font.pixelSize: 10
                            wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }
                    }

                    onOpened: importNameField.forceActiveFocus()
                }

                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    radius: 8
                    implicitHeight: backendGrid.implicitHeight + 20
                    GridLayout {
                        id: backendGrid
                        anchors.fill: parent; anchors.margins: 10
                        columns: 2; rowSpacing: 8; columnSpacing: 10

                        Text { text: "Binary"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcComboBox {
                            id: backendBinary
                            Layout.fillWidth: true
                            model: App.binaryRegistry
                            textRole: "displayLabel"; valueRole: "binId"
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text { text: backendBinary.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                        }
                        Text { text: "Host"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: backendHost; Layout.fillWidth: true }
                        Text { text: "Port"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: backendPort; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    radius: 8
                    implicitHeight: modelGrid.implicitHeight + 20
                    GridLayout {
                        id: modelGrid
                        anchors.fill: parent; anchors.margins: 10
                        columns: 3; rowSpacing: 8; columnSpacing: 10

                        Item { implicitWidth: 20 }
                        Text { text: "Main model"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcComboBox {
                            id: modelMain
                            Layout.fillWidth: true
                            model: App.modelCatalog; textRole: "fileName"; valueRole: "modelId"
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text { text: modelMain.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                        }

                        CheckBox { id: mmprojCheck; checked: mmprojEnabled; onCheckedChanged: mmprojEnabled = checked; padding: 0 }
                        Text { text: "mmproj"; color: mmprojEnabled ? Theme.textSecondary : Theme.textMuted; font.pixelSize: 12 }
                        LcComboBox {
                            id: modelMmproj
                            Layout.fillWidth: true
                            enabled: mmprojEnabled; opacity: mmprojEnabled ? 1.0 : 0.4
                            model: App.modelCatalog; textRole: "fileName"; valueRole: "modelId"
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text { text: modelMmproj.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                        }

                        CheckBox { id: draftCheck; checked: draftEnabled; onCheckedChanged: draftEnabled = checked; padding: 0 }
                        Text { text: "Draft model"; color: draftEnabled ? Theme.textSecondary : Theme.textMuted; font.pixelSize: 12 }
                        LcComboBox {
                            id: modelDraft
                            Layout.fillWidth: true
                            enabled: draftEnabled; opacity: draftEnabled ? 1.0 : 0.4
                            model: App.modelCatalog; textRole: "fileName"; valueRole: "modelId"
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text { text: modelDraft.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    radius: 8
                    implicitHeight: runtimeGrid.implicitHeight + 20
                    GridLayout {
                        id: runtimeGrid
                        anchors.fill: parent; anchors.margins: 10
                        columns: 4; rowSpacing: 8; columnSpacing: 10

                        Text { text: "ctx"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: ctxField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "batch"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: batchField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "ubatch"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: ubatchField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "threads"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: threadsField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "gpuLayers"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: gpuLayersField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "parallelSlots"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: parallelSlotsField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "cacheType"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: cacheTypeField; Layout.fillWidth: true }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    CheckBox { id: flashAttnCheck; text: "flash-attn"; contentItem: Text { text: parent.text; color: Theme.textSecondary; leftPadding: parent.indicator.width + 6 } }
                    CheckBox { id: mmapCheck; text: "mmap"; contentItem: Text { text: parent.text; color: Theme.textSecondary; leftPadding: parent.indicator.width + 6 } }
                    CheckBox { id: mlockCheck; text: "mlock"; contentItem: Text { text: parent.text; color: Theme.textSecondary; leftPadding: parent.indicator.width + 6 } }
                    CheckBox { id: contBatchCheck; text: "cont-batching"; contentItem: Text { text: parent.text; color: Theme.textSecondary; leftPadding: parent.indicator.width + 6 } }
                }

                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    radius: 8
                    implicitHeight: advancedGrid.implicitHeight + 20
                    GridLayout {
                        id: advancedGrid
                        anchors.fill: parent; anchors.margins: 10
                        columns: 4; rowSpacing: 8; columnSpacing: 10

                        Text { text: "alias"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: aliasField; Layout.fillWidth: true }
                        Text { text: "n-predict"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: nPredictField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "cache-type-v"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: cacheTypeVField; Layout.fillWidth: true }
                        Text { text: "temp"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: tempField; Layout.fillWidth: true }
                        Text { text: "top-p"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: topPField; Layout.fillWidth: true }
                        Text { text: "top-k"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: topKField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "repeat-penalty"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: repeatPenaltyField; Layout.fillWidth: true }
                        Text { text: "presence-penalty"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: presencePenaltyField; Layout.fillWidth: true }
                        Text { text: "cache-ram"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: cacheRamField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "cache-reuse"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: cacheReuseField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    CheckBox { id: contextShiftCheck;   text: "context-shift";    checked: true; contentItem: Text { text: parent.text; color: Theme.textSecondary; leftPadding: parent.indicator.width + 6 } }
                    CheckBox { id: metricsCheck; text: "metrics"; contentItem: Text { text: parent.text; color: Theme.textSecondary; leftPadding: parent.indicator.width + 6 } }
                    CheckBox { id: noWarmupCheck; text: "no-warmup"; contentItem: Text { text: parent.text; color: Theme.textSecondary; leftPadding: parent.indicator.width + 6 } }
                }

                // ── Harness ──────────────────────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    radius: 8
                    implicitHeight: harnessCol.implicitHeight + 20

                    ColumnLayout {
                        id: harnessCol
                        anchors { fill: parent; margins: 12 }
                        spacing: 10

                        Text {
                            text: (App.langV, App.l("harness.title"))
                            color: Theme.textSecondary
                            font.pixelSize: 12
                        }

                        // Selección por tarjetas (sin dropdown).

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Repeater {
                                model: [
                                    { adapter: "none",      label: (App.langV, App.l("harness.none")),  icon: "—" },
                                    { adapter: "opencode",  label: "Opencode",   icon: "🔮" },
                                    { adapter: "llamaagent", label: "LlamaAgent", icon: "🛠" },
                                    // Ocultos por ahora — reactivar agregando al modelo:
                                    // { adapter: "raw",       label: "Raw Chat",   icon: "💬" },
                                    // { adapter: "smallcode", label: "Smallcode",  icon: "🧩" },
                                    // { adapter: "pi",        label: "Pi",         icon: "🥧" },
                                ]

                                delegate: Rectangle {
                                    Layout.fillWidth: true
                                    height: modelData.adapter === "none" ? 52 : 82
                                    radius: 8
                                    color: harnessAdapter === modelData.adapter ? Theme.highlight : Theme.inputBg
                                    border.color: harnessAdapter === modelData.adapter ? Theme.accent : Theme.borderColor
                                    border.width: harnessAdapter === modelData.adapter ? 2 : 1
                                    clip: true

                                    // install status for non-none options
                                    readonly property bool isInstalled: modelData.adapter === "none"
                                        ? true
                                        : (App.harnessCheckV, App.isHarnessInstalled(modelData.adapter))

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: harnessAdapter = modelData.adapter
                                    }

                                    ColumnLayout {
                                        anchors { fill: parent; margins: 8 }
                                        spacing: 4

                                        Row {
                                            spacing: 6
                                            Layout.fillWidth: true
                                            Text { text: modelData.icon; font.pixelSize: 16 }
                                            Text {
                                                text: modelData.label
                                                color: Theme.textPrimary
                                                font { pixelSize: 13; bold: true }
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                        }

                                        // install status row (non-none only)
                                        RowLayout {
                                            visible: modelData.adapter !== "none"
                                            Layout.fillWidth: true
                                            spacing: 6

                                            Rectangle {
                                                width: 7; height: 7; radius: 4
                                                color: parent.visible
                                                    ? (isInstalled ? Theme.successText : Theme.errorText)
                                                    : "transparent"
                                            }
                                            Text {
                                                text: {
                                                    const _lang = App.langV
                                                    if (!parent.visible) return ""
                                                    return isInstalled
                                                        ? App.l("harness.installed")
                                                        : App.l("harness.notInstalled")
                                                }
                                                color: isInstalled ? Theme.successText : Theme.textMuted
                                                font.pixelSize: 11
                                                Layout.fillWidth: true
                                            }

                                            LcButton {
                                                visible: modelData.adapter !== "none" && !isInstalled
                                                text: {
                                                    const _lang = App.langV
                                                    return App.installingHarness
                                                        ? App.l("harness.installing")
                                                        : App.l("harness.install")
                                                }
                                                enabled: !App.installingHarness
                                                onClicked: App.installHarness(modelData.adapter)
                                                implicitHeight: 26
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // install status message
                        Text {
                            visible: App.harnessInstallStatus.length > 0
                            text: App.harnessInstallStatus
                            color: Theme.textMuted
                            font.pixelSize: 11
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                        }

                        Connections {
                            target: App
                            function onHarnessInstallFinished(success, adapter, message) {
                                if (success && harnessAdapter === adapter) {
                                    // auto-save on successful install
                                    saveAll()
                                }
                            }
                        }
                    }
                }

                // ─────────────────────────────────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    radius: 8
                    implicitHeight: 220
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 10
                        Text { text: (App.langV, App.l("profiles.extraArgs")); color: Theme.textSecondary; font.pixelSize: 12 }
                        ScrollView {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            clip: true
                            ScrollBar.vertical.policy: ScrollBar.AsNeeded
                            TextArea {
                                id: manualExtraArgsArea
                                color: Theme.textPrimary
                                wrapMode: TextArea.WrapAtWordBoundaryOrAnywhere
                                background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            }
                        }
                    }
                }

                // ── Maestro (supervisor) ─────────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    radius: 8
                    implicitHeight: masterCol.implicitHeight + 20
                    ColumnLayout {
                        id: masterCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 8

                        Text {
                            text: "Maestro (supervisor)"
                            color: Theme.textSecondary; font { pixelSize: 12; bold: true }
                        }
                        Text {
                            text: "Modelo más capaz al que el LLM local escala un problema que no resuelve."
                            color: Theme.textMuted; font.pixelSize: 11
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Text { text: "Tipo"; color: Theme.textSecondary; font.pixelSize: 12
                                   Layout.preferredWidth: 90 }
                            LcComboBox {
                                id: masterKindCombo
                                Layout.fillWidth: true
                                implicitHeight: 32
                                model: [
                                    { value: "none", label: "Ninguno" },
                                    { value: "cli",  label: "CLI local (Claude Code / Codex)" },
                                    { value: "http", label: "Endpoint HTTP (OpenAI-compat)" }
                                ]
                                textRole: "label"
                                valueRole: "value"
                                currentIndex: Math.max(0, indexOfValue(masterKind))
                                onActivated: masterKind = currentValue
                            }
                        }

                        // ── Modo CLI ──────────────────────────────────────────
                        ColumnLayout {
                            visible: masterKind === "cli"
                            Layout.fillWidth: true; spacing: 6

                            RowLayout {
                                Layout.fillWidth: true; spacing: 8
                                Text { text: "CLI"; color: Theme.textSecondary; font.pixelSize: 12
                                       Layout.preferredWidth: 90 }
                                LcComboBox {
                                    id: masterCliCombo
                                    Layout.fillWidth: true
                                    implicitHeight: 32
                                    model: App.masterCliList()
                                    currentIndex: Math.max(0, indexOfValue(masterCliName))
                                    onActivated: masterCliName = currentValue
                                }
                                LcButton {
                                    text: "Detectar"; secondary: true; implicitHeight: 30
                                    onClicked: refreshMasterCliStatus(true)
                                }
                            }

                            // Estado de instalación del CLI seleccionado
                            RowLayout {
                                Layout.fillWidth: true; spacing: 6
                                property var st: masterCliStatusCache[masterCliName] ?? ({})
                                Rectangle {
                                    width: 7; height: 7; radius: 4
                                    color: (parent.st.installed === true) ? Theme.successText : Theme.errorText
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: (parent.st.installed === true)
                                        ? ((parent.st.label ?? masterCliName) + " — " + (parent.st.version ?? "instalado"))
                                        : ((parent.st.label ?? masterCliName) + " no detectado en PATH")
                                    color: (parent.st.installed === true) ? Theme.successText : Theme.textMuted
                                    font.pixelSize: 11; wrapMode: Text.WrapAnywhere
                                }
                            }
                            // Comando de instalación (si no está)
                            RowLayout {
                                Layout.fillWidth: true; spacing: 6
                                visible: (masterCliStatusCache[masterCliName] ?? {}).installed !== true
                                Text {
                                    Layout.fillWidth: true
                                    text: App.masterCliInstallCommand(masterCliName)
                                    color: Theme.textMuted; font { pixelSize: 11; family: "monospace" }
                                    wrapMode: Text.WrapAnywhere
                                }
                                LcButton {
                                    text: "Copiar"; secondary: true; implicitHeight: 28
                                    onClicked: App.copyToClipboard(App.masterCliInstallCommand(masterCliName))
                                }
                            }

                            CheckBox {
                                id: masterApplyEditsCheck
                                text: "El maestro edita archivos directo (sino, sólo devuelve plan)"
                                checked: masterApplyEdits
                                onToggled: masterApplyEdits = checked
                                contentItem: Text {
                                    text: masterApplyEditsCheck.text; color: Theme.textPrimary
                                    font.pixelSize: 12; leftPadding: masterApplyEditsCheck.indicator.width + 6
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }

                        // ── Modo HTTP ─────────────────────────────────────────
                        ColumnLayout {
                            visible: masterKind === "http"
                            Layout.fillWidth: true; spacing: 6
                            LcTextField { id: masterHttpUrlField;   Layout.fillWidth: true
                                          placeholderText: "Endpoint (ej. https://api.openai.com)" }
                            LcTextField { id: masterHttpModelField; Layout.fillWidth: true
                                          placeholderText: "Modelo (ej. gpt-4o)" }
                            LcTextField { id: masterHttpKeyField;   Layout.fillWidth: true
                                          echoMode: TextInput.Password
                                          placeholderText: "API key (opcional)" }
                        }

                        // ── Escalado ──────────────────────────────────────────
                        RowLayout {
                            visible: masterKind !== "none"
                            Layout.fillWidth: true; spacing: 8
                            Text { text: "Escalado"; color: Theme.textSecondary; font.pixelSize: 12
                                   Layout.preferredWidth: 90 }
                            LcComboBox {
                                id: masterEscalationCombo
                                Layout.fillWidth: true
                                implicitHeight: 32
                                model: [
                                    { value: "manual", label: "Manual (botón en el agente)" },
                                    { value: "auto",   label: "Automático (al atascarse)" },
                                    { value: "both",   label: "Ambos" }
                                ]
                                textRole: "label"
                                valueRole: "value"
                                currentIndex: Math.max(0, indexOfValue(masterEscalation))
                                onActivated: masterEscalation = currentValue
                            }
                        }
                        RowLayout {
                            visible: masterKind !== "none" && masterEscalation !== "manual"
                            Layout.fillWidth: true; spacing: 8
                            Text { text: "Auto tras N fallos"; color: Theme.textSecondary; font.pixelSize: 12
                                   Layout.preferredWidth: 130 }
                            SpinBox {
                                from: 1; to: 20; value: masterAutoAfterFails
                                onValueModified: masterAutoAfterFails = value
                                implicitHeight: 32
                            }
                        }
                    }
                }

                // ── Vista previa del comando ─────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    radius: 8
                    implicitHeight: previewCol.implicitHeight + 20
                    ColumnLayout {
                        id: previewCol
                        anchors.fill: parent; anchors.margins: 10
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: (App.langV, App.l("launch.cmdPreview"))
                                color: Theme.textSecondary; font.pixelSize: 12
                                Layout.fillWidth: true
                            }
                            LcButton {
                                text: "Recalcular comando"
                                secondary: true
                                onClicked: recomputePreview()
                            }
                        }
                        CommandPreview {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 140
                            visible: App.effectiveProfile.launchId === selectedLaunchId
                            commandLine: App.effectiveProfile.commandLine ?? ""
                            warnings: App.effectiveProfile.warnings ?? []
                            errors: App.effectiveProfile.blockingErrors ?? []
                        }
                    }
                }

                Item { Layout.fillHeight: true; Layout.preferredHeight: 16 }
                }
            }
        }
    }
}
