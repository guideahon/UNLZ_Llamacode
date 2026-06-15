import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0
import "../components"

// Modo Charla (voz-a-voz): hablar con la IA y escuchar la respuesta.
// STT→backend de chat→TTS. La config (local/cloud) vive en App.voiceConfig().
Item {
    id: page

    property var cfg: ({})
    function reload() { cfg = App.voiceConfig() }
    function save() { App.setVoiceConfig(cfg) }
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

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 12
                    LcButton {
                        text: App.voiceActive ? "Detener" : "Iniciar charla"
                        danger: App.voiceActive
                        onClicked: App.voiceActive ? App.stopCharla() : App.startCharla()
                    }
                    LcButton {
                        text: "Hablar ahora"
                        secondary: true
                        enabled: App.voiceActive
                        onClicked: App.charlaListen()
                    }
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

                    Text { text: "Reconocimiento de voz (STT)"; color: Theme.textPrimary; Layout.leftMargin: 24; font { pixelSize: 15; bold: true } }
                    GridLayout {
                        columns: 2; columnSpacing: 12; rowSpacing: 8
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.fillWidth: true

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

                    Text { text: "Síntesis de voz (TTS)"; color: Theme.textPrimary; Layout.leftMargin: 24; font { pixelSize: 15; bold: true } }
                    GridLayout {
                        columns: 2; columnSpacing: 12; rowSpacing: 8
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.fillWidth: true

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
