import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0
import "../components"

// Modo Charla (voz-a-voz): hablar con la IA y escuchar la respuesta.
// STT→backend de chat→TTS. La config (local/cloud) vive en App.voiceConfig().
Item {
    id: page

    // La config de voz vive en el LaunchProfile activo (el que lanzó el server).
    property string pid: App.activeLaunchId
    property var cfg: ({})
    property bool testing: false
    property int installPct: -1
    property string installMsg: ""
    property string whisperPath: App.voiceWhisperServerPath()
    property string piperPath: App.voicePiperPath()

    Connections {
        target: App
        function onVoiceInstallProgress(engineId, pct, status) { page.installPct = pct; page.installMsg = status }
        function onVoiceInstallFinished(engineId, ok, message) {
            page.installPct = -1
            page.installMsg = ok ? "Instalado ✓" : ("Error: " + message)
            sttEngineCombo.refresh()
            ttsVoiceCombo.refresh()
        }
        function onVoiceBinaryInstalled(kind, ok, message) {
            page.whisperPath = App.voiceWhisperServerPath()
            page.piperPath = App.voicePiperPath()
            page.installMsg = ok ? "Binario instalado ✓" : ("Error binario: " + message)
        }
    }
    function reload() { cfg = pid.length ? App.voiceConfig(pid) : ({}) }
    function save() { if (pid.length) App.setVoiceConfig(pid, cfg) }
    onPidChanged: reload()
    Component.onCompleted: reload()

    readonly property string st: App.voiceState
    readonly property var stateColor: ({
        "idle": Theme.textMuted, "listening": Theme.accent,
        "transcribing": "#e0a93b", "thinking": "#9b6dd6",
        "speaking": "#3bbf6e", "error": Theme.btnDangerBg
    })
    readonly property var stateLabel: ({
        "idle": "Listo", "listening": "Escuchando…", "transcribing": "Transcribiendo…",
        "thinking": "Pensando…", "speaking": "Hablando…", "error": "Error"
    })

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader { title: "🎙  Charla"; subtitle: "Conversación por voz, 100% local u opcionalmente cloud" }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ── Panel izquierdo: orbe + controles ──
            ColumnLayout {
                Layout.preferredWidth: 360
                Layout.fillHeight: true
                Layout.margins: 24
                spacing: 20

                Item { Layout.fillHeight: true }

                // Orbe de estado con anillo de nivel.
                Item {
                    Layout.alignment: Qt.AlignHCenter
                    width: 220; height: 220

                    Rectangle {            // anillo de nivel de micrófono
                        anchors.centerIn: parent
                        width: 160 + App.voiceLevel * 600
                        height: width
                        radius: width / 2
                        color: "transparent"
                        border.color: page.stateColor[page.st] || Theme.textMuted
                        border.width: 2
                        opacity: page.st === "listening" || page.st === "speaking" ? 0.6 : 0.15
                        Behavior on width { NumberAnimation { duration: 90 } }
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 150; height: 150; radius: 75
                        color: page.stateColor[page.st] || Theme.textMuted
                        opacity: 0.9
                        Text {
                            anchors.centerIn: parent
                            text: page.st === "speaking" ? "🔊" : "🎙"
                            font.pixelSize: 56
                        }
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: page.stateLabel[page.st] || page.st
                    color: Theme.textPrimary
                    font { pixelSize: 18; bold: true }
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    visible: App.voiceError.length > 0
                    text: App.voiceError
                    color: Theme.btnDangerBg
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.maximumWidth: 320
                    horizontalAlignment: Text.AlignHCenter
                }

                // Transcripción parcial en vivo del turno en curso.
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: 320
                    visible: App.voicePartial.length > 0
                    text: "“" + App.voicePartial + "”"
                    color: Theme.textSecondary
                    font { pixelSize: 13; italic: true }
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }

                // Medidor de nivel de micrófono (siempre visible).
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: "Nivel de entrada"; color: Theme.textMuted; font.pixelSize: 11 }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 12; radius: 6
                        color: Theme.inputBg
                        border.color: Theme.borderColor
                        Rectangle {
                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 2 }
                            width: Math.max(0, Math.min(1, App.voiceLevel * 6)) * (parent.width - 4)
                            radius: 5
                            color: App.voiceLevel > 0.03 ? "#3bbf6e" : Theme.accent
                            Behavior on width { NumberAnimation { duration: 70 } }
                        }
                    }
                }

                // Selección de micrófono.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: "Micrófono"; color: Theme.textMuted; font.pixelSize: 11 }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        LcComboBox {
                            id: micCombo
                            Layout.fillWidth: true
                            property var devs: App.audioInputDevices()
                            textRole: "name"
                            model: devs
                            Component.onCompleted: {
                                var cur = App.voiceInputDevice()
                                for (var i = 0; i < devs.length; ++i)
                                    if (devs[i].id === cur) { currentIndex = i; break }
                            }
                            onActivated: if (devs[currentIndex]) App.setVoiceInputDevice(devs[currentIndex].id)
                        }
                        LcButton {
                            text: "↻"
                            secondary: true
                            onClicked: { micCombo.devs = App.audioInputDevices(); micCombo.model = micCombo.devs }
                        }
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 12
                    LcButton {
                        text: (App.voiceActive && !page.testing) ? "Detener" : "Iniciar charla"
                        danger: App.voiceActive && !page.testing
                        enabled: !page.testing
                        onClicked: {
                            if (App.voiceActive) App.stopCharla()
                            else App.startCharla()
                        }
                    }
                    LcButton {
                        text: page.testing ? "Detener prueba" : "Probar micrófono"
                        secondary: true
                        enabled: !App.voiceActive || page.testing
                        onClicked: {
                            if (page.testing) { App.stopMicTest(); page.testing = false }
                            else { App.startMicTest(); page.testing = true }
                        }
                    }
                }
                LcButton {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Hablar ahora"
                    secondary: true
                    visible: App.voiceActive && !page.testing
                    onClicked: App.charlaListen()
                }

                Item { Layout.fillHeight: true }
            }

            Rectangle { width: 1; Layout.fillHeight: true; color: Theme.divider }

            // ── Panel derecho: configuración ──
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true

                ColumnLayout {
                    width: parent.width
                    spacing: 14

                    Item { height: 8; width: 1 }

                    Text { text: "Motor STT gestionado (la app descarga y lanza)"; color: Theme.textPrimary; Layout.leftMargin: 24; font { pixelSize: 15; bold: true } }
                    GridLayout {
                        columns: 2; columnSpacing: 12; rowSpacing: 8
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.fillWidth: true

                        Text { text: "Motor"; color: Theme.textSecondary }
                        LcComboBox {
                            id: sttEngineCombo
                            Layout.fillWidth: true
                            property var opts: [{ id: "", name: "Manual (endpoint propio, abajo)" }].concat(App.voiceSttCatalog())
                            function refresh() { opts = [{ id: "", name: "Manual (endpoint propio, abajo)" }].concat(App.voiceSttCatalog()) }
                            textRole: "name"
                            model: opts
                            Component.onCompleted: {
                                var cur = page.cfg.sttManagedEngine || ""
                                for (var i = 0; i < opts.length; ++i) if (opts[i].id === cur) { currentIndex = i; break }
                            }
                            onActivated: { page.cfg.sttManagedEngine = opts[currentIndex].id; page.save() }
                        }

                        Text { text: "Estado"; color: Theme.textSecondary; visible: (page.cfg.sttManagedEngine || "") !== "" }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            visible: (page.cfg.sttManagedEngine || "") !== ""
                            Text {
                                Layout.fillWidth: true
                                color: Theme.textMuted; font.pixelSize: 12
                                text: page.installPct >= 0 ? page.installMsg
                                      : (App.voiceModelInstalled(page.cfg.sttManagedEngine || "") ? "Instalado ✓"
                                         : (page.installMsg.length ? page.installMsg : "No instalado"))
                            }
                            LcButton {
                                text: page.installPct >= 0 ? "Cancelar" : "Instalar modelo"
                                secondary: true
                                visible: page.installPct >= 0 || !App.voiceModelInstalled(page.cfg.sttManagedEngine || "")
                                onClicked: {
                                    if (page.installPct >= 0) App.cancelVoiceModelInstall()
                                    else App.installVoiceModel(page.cfg.sttManagedEngine)
                                }
                            }
                        }

                        Text { text: "Binario whisper-server"; color: Theme.textSecondary; visible: (page.cfg.sttManagedEngine || "") !== "" }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            visible: (page.cfg.sttManagedEngine || "") !== ""
                            LcTextField {
                                Layout.fillWidth: true
                                placeholderText: "ruta a whisper-server (vacío = PATH)"
                                text: page.whisperPath
                                onEditingFinished: { App.setVoiceWhisperServerPath(text); page.whisperPath = text }
                            }
                            LcButton { text: "Descargar"; secondary: true; onClicked: App.installVoiceBinary("whisper-server", "") }
                            LcButton { text: "…"; secondary: true; onClicked: { App.pickVoiceWhisperServer(); page.whisperPath = App.voiceWhisperServerPath() } }
                        }
                    }

                    Text {
                        text: "Reconocimiento de voz (STT) — endpoint manual"
                        color: Theme.textPrimary; Layout.leftMargin: 24; font { pixelSize: 15; bold: true }
                        visible: (page.cfg.sttManagedEngine || "") === ""
                    }
                    GridLayout {
                        columns: 2; columnSpacing: 12; rowSpacing: 8
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.fillWidth: true
                        visible: (page.cfg.sttManagedEngine || "") === ""

                        Text { text: "Proveedor"; color: Theme.textSecondary }
                        LcComboBox {
                            Layout.fillWidth: true
                            model: ["local", "cloud"]
                            currentIndex: (page.cfg.sttProvider === "cloud") ? 1 : 0
                            onActivated: { page.cfg.sttProvider = model[currentIndex]; page.save() }
                        }
                        Text { text: "Endpoint (baseUrl)"; color: Theme.textSecondary }
                        LcTextField {
                            Layout.fillWidth: true
                            text: page.cfg.sttBaseUrl || ""
                            onEditingFinished: { page.cfg.sttBaseUrl = text; page.save() }
                        }
                        Text { text: "Modelo"; color: Theme.textSecondary }
                        LcTextField {
                            Layout.fillWidth: true
                            text: page.cfg.sttModel || ""
                            onEditingFinished: { page.cfg.sttModel = text; page.save() }
                        }
                        Text { text: "Idioma"; color: Theme.textSecondary }
                        LcTextField {
                            Layout.fillWidth: true
                            text: page.cfg.sttLanguage || "auto"
                            onEditingFinished: { page.cfg.sttLanguage = text; page.save() }
                        }
                        Text { text: "API key ref"; color: Theme.textSecondary; visible: page.cfg.sttProvider === "cloud" }
                        LcTextField {
                            Layout.fillWidth: true
                            visible: page.cfg.sttProvider === "cloud"
                            placeholderText: "ej: voice/openai (env var o store)"
                            text: page.cfg.sttKeyRef || ""
                            onEditingFinished: { page.cfg.sttKeyRef = text; page.save() }
                        }
                    }

                    Text { text: "Motor TTS gestionado (piper, local)"; color: Theme.textPrimary; Layout.leftMargin: 24; font { pixelSize: 15; bold: true } }
                    GridLayout {
                        columns: 2; columnSpacing: 12; rowSpacing: 8
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.fillWidth: true

                        Text { text: "Modo"; color: Theme.textSecondary }
                        LcComboBox {
                            Layout.fillWidth: true
                            model: ["http", "piper"]
                            currentIndex: (page.cfg.ttsMode === "piper") ? 1 : 0
                            onActivated: { page.cfg.ttsMode = model[currentIndex]; page.save() }
                        }

                        Text { text: "Voz piper"; color: Theme.textSecondary; visible: page.cfg.ttsMode === "piper" }
                        LcComboBox {
                            id: ttsVoiceCombo
                            Layout.fillWidth: true
                            visible: page.cfg.ttsMode === "piper"
                            property var opts: App.voiceTtsCatalog()
                            function refresh() { opts = App.voiceTtsCatalog() }
                            textRole: "name"
                            model: opts
                            Component.onCompleted: {
                                var cur = page.cfg.ttsManagedVoice || ""
                                for (var i = 0; i < opts.length; ++i) if (opts[i].id === cur) { currentIndex = i; break }
                            }
                            onActivated: { page.cfg.ttsManagedVoice = opts[currentIndex].id; page.save() }
                        }

                        Text { text: "Estado voz"; color: Theme.textSecondary; visible: page.cfg.ttsMode === "piper" }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            visible: page.cfg.ttsMode === "piper"
                            Text {
                                Layout.fillWidth: true; color: Theme.textMuted; font.pixelSize: 12
                                text: App.voiceTtsVoiceInstalled(page.cfg.ttsManagedVoice || "") ? "Instalada ✓" : "No instalada"
                            }
                            LcButton {
                                text: "Instalar voz"
                                secondary: true
                                visible: (page.cfg.ttsManagedVoice || "") !== "" && !App.voiceTtsVoiceInstalled(page.cfg.ttsManagedVoice || "")
                                onClicked: App.installVoiceTts(page.cfg.ttsManagedVoice)
                            }
                        }

                        Text { text: "Binario piper"; color: Theme.textSecondary; visible: page.cfg.ttsMode === "piper" }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            visible: page.cfg.ttsMode === "piper"
                            LcTextField {
                                Layout.fillWidth: true
                                placeholderText: "ruta a piper (vacío = PATH)"
                                text: page.piperPath
                                onEditingFinished: { App.setVoicePiperPath(text); page.piperPath = text }
                            }
                            LcButton { text: "Descargar"; secondary: true; onClicked: App.installVoiceBinary("piper", "") }
                            LcButton { text: "…"; secondary: true; onClicked: { App.pickVoicePiper(); page.piperPath = App.voicePiperPath() } }
                        }
                    }

                    Text {
                        text: "Síntesis de voz (TTS) — endpoint HTTP"
                        color: Theme.textPrimary; Layout.leftMargin: 24; font { pixelSize: 15; bold: true }
                        visible: page.cfg.ttsMode !== "piper"
                    }
                    GridLayout {
                        columns: 2; columnSpacing: 12; rowSpacing: 8
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.fillWidth: true
                        visible: page.cfg.ttsMode !== "piper"

                        Text { text: "Proveedor"; color: Theme.textSecondary }
                        LcComboBox {
                            Layout.fillWidth: true
                            model: ["local", "cloud"]
                            currentIndex: (page.cfg.ttsProvider === "cloud") ? 1 : 0
                            onActivated: { page.cfg.ttsProvider = model[currentIndex]; page.save() }
                        }
                        Text { text: "Endpoint (baseUrl)"; color: Theme.textSecondary }
                        LcTextField {
                            Layout.fillWidth: true
                            text: page.cfg.ttsBaseUrl || ""
                            onEditingFinished: { page.cfg.ttsBaseUrl = text; page.save() }
                        }
                        Text { text: "Modelo"; color: Theme.textSecondary }
                        LcTextField {
                            Layout.fillWidth: true
                            text: page.cfg.ttsModel || ""
                            onEditingFinished: { page.cfg.ttsModel = text; page.save() }
                        }
                        Text { text: "Voz"; color: Theme.textSecondary }
                        LcTextField {
                            Layout.fillWidth: true
                            text: page.cfg.ttsVoice || ""
                            onEditingFinished: { page.cfg.ttsVoice = text; page.save() }
                        }
                        Text { text: "Formato"; color: Theme.textSecondary }
                        LcComboBox {
                            Layout.fillWidth: true
                            model: ["wav", "mp3", "pcm"]
                            currentIndex: Math.max(0, model.indexOf(page.cfg.ttsFormat || "wav"))
                            onActivated: { page.cfg.ttsFormat = model[currentIndex]; page.save() }
                        }
                        Text { text: "API key ref"; color: Theme.textSecondary; visible: page.cfg.ttsProvider === "cloud" }
                        LcTextField {
                            Layout.fillWidth: true
                            visible: page.cfg.ttsProvider === "cloud"
                            text: page.cfg.ttsKeyRef || ""
                            onEditingFinished: { page.cfg.ttsKeyRef = text; page.save() }
                        }
                    }

                    Text { text: "Detección de habla (VAD)"; color: Theme.textPrimary; Layout.leftMargin: 24; font { pixelSize: 15; bold: true } }
                    GridLayout {
                        columns: 2; columnSpacing: 12; rowSpacing: 8
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.fillWidth: true

                        Text { text: "Silencio fin de turno (ms)"; color: Theme.textSecondary }
                        LcTextField {
                            Layout.fillWidth: true
                            text: String(page.cfg.vadSilenceMs || 800)
                            onEditingFinished: { page.cfg.vadSilenceMs = parseInt(text) || 800; page.save() }
                        }
                        Text { text: "Segmento incremental (ms)"; color: Theme.textSecondary }
                        LcTextField {
                            Layout.fillWidth: true
                            text: String(page.cfg.vadSegmentMs || 350)
                            onEditingFinished: { page.cfg.vadSegmentMs = parseInt(text) || 350; page.save() }
                        }
                        Text { text: "Auto-escuchar"; color: Theme.textSecondary }
                        Switch {
                            checked: page.cfg.autoListen !== false
                            onToggled: { page.cfg.autoListen = checked; page.save() }
                        }
                        Text { text: "Barge-in (interrumpir)"; color: Theme.textSecondary }
                        Switch {
                            checked: page.cfg.bargeIn !== false
                            onToggled: { page.cfg.bargeIn = checked; page.save() }
                        }
                    }

                    Item { height: 16; width: 1 }
                }
            }
        }
    }
}
