import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Item {
    id: root

    property var selectedIds: []

    function toggleProfile(profileId) {
        const idx = selectedIds.indexOf(profileId)
        if (idx >= 0) {
            const copy = selectedIds.slice(); copy.splice(idx, 1); selectedIds = copy
        } else {
            selectedIds = selectedIds.concat([profileId])
        }
    }

    Component.onCompleted: App.loadBenchmarkResults ? App.loadBenchmarkResults() : null

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: (App.langV, App.l("nav.benchmark"))
            subtitle: App.benchmarkRunning
                ? App.benchmarkStatus
                : "Compará perfiles por RAM, velocidad y calidad"
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ── Panel izquierdo ────────────────────────────────────────────
            Rectangle {
                Layout.preferredWidth: 260
                Layout.fillHeight: true
                color: Theme.surfaceBg

                ColumnLayout {
                    anchors { fill: parent; margins: 16 }
                    spacing: 14

                    Text {
                        text: "MODO DE PRUEBA"
                        color: Theme.textSecondary
                        font.pixelSize: 10; font.bold: true
                    }

                    ColumnLayout {
                        spacing: 4
                        RadioButton { id: shortMode; text: "Corta (~2 min)"; checked: true }
                        Text { text: "2 speed + 5 quality tasks"; color: Theme.textMuted; font.pixelSize: 11; leftPadding: 24 }
                        Item { height: 2 }
                        RadioButton { id: fullMode; text: "Completa (~5 min)" }
                        Text { text: "5 speed + 7 quality tasks"; color: Theme.textMuted; font.pixelSize: 11; leftPadding: 24 }
                    }

                    Rectangle { height: 1; Layout.fillWidth: true; color: Theme.divider }

                    Text {
                        text: "PERFILES A COMPARAR"
                        color: Theme.textSecondary
                        font.pixelSize: 10; font.bold: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 200
                        color: Theme.inputBg
                        radius: 6
                        border.color: Theme.divider; border.width: 1
                        clip: true

                        ListView {
                            id: profileList
                            anchors { fill: parent; margins: 4 }
                            model: App.profileManager.launchProfiles
                            spacing: 2; clip: true

                            delegate: Item {
                                id: pd
                                width: profileList.width; height: 32
                                property string profileId: model.id ?? ""
                                property bool isSelected: root.selectedIds.indexOf(profileId) >= 0

                                Rectangle {
                                    anchors.fill: parent; radius: 4
                                    color: pd.isSelected ? Theme.highlight
                                         : (pdHover.containsMouse ? Theme.hoverBg : "transparent")
                                }
                                HoverHandler { id: pdHover }

                                Row {
                                    anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                                    spacing: 6
                                    CheckBox {
                                        checked: pd.isSelected
                                        anchors.verticalCenter: parent.verticalCenter
                                        onToggled: root.toggleProfile(pd.profileId)
                                    }
                                    Text {
                                        text: model.name || "(sin nombre)"
                                        color: Theme.textPrimary; font.pixelSize: 12
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: profileList.width - 60; elide: Text.ElideRight
                                    }
                                }
                                TapHandler { onTapped: root.toggleProfile(pd.profileId) }
                            }
                            ScrollBar.vertical: ScrollBar {}
                        }
                    }

                    // Progress bar (visible durante benchmark)
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: App.benchmarkRunning

                        ProgressBar {
                            Layout.fillWidth: true
                            value: App.benchmarkProgress / 100.0
                            background: Rectangle { color: Theme.inputBg; radius: 3 }
                            contentItem: Item {
                                Rectangle {
                                    width: parent.width * parent.parent.value
                                    height: parent.height; radius: 3
                                    color: Theme.accent
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: App.benchmarkStatus
                            color: Theme.textMuted; font.pixelSize: 10
                            wrapMode: Text.Wrap; elide: Text.ElideRight
                        }
                    }

                    Item { Layout.fillHeight: true }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        LcButton {
                            Layout.fillWidth: true
                            text: App.benchmarkRunning ? "Cancelar" : "Iniciar benchmark"
                            danger: App.benchmarkRunning
                            enabled: App.benchmarkRunning || root.selectedIds.length > 0
                            onClicked: {
                                if (App.benchmarkRunning) {
                                    App.cancelBenchmark()
                                } else {
                                    App.startBenchmark(root.selectedIds, shortMode.checked ? "short" : "full")
                                }
                            }
                        }

                        LcButton {
                            text: "Limpiar"
                            secondary: true
                            visible: App.benchmarkResults.length > 0 && !App.benchmarkRunning
                            onClicked: App.clearBenchmarkResults()
                        }
                    }
                }
            }

            Rectangle { width: 1; Layout.fillHeight: true; color: Theme.divider }

            // ── Panel derecho: resultados ──────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.baseBg
                clip: true

                ColumnLayout {
                    anchors { fill: parent; margins: 20 }
                    spacing: 12

                    // Cabecera tabla
                    Rectangle {
                        Layout.fillWidth: true
                        height: 30
                        color: Theme.surfaceBg; radius: 6

                        RowLayout {
                            anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                            spacing: 0
                            Text { text: "Perfil"; color: Theme.textSecondary; font.pixelSize: 11; Layout.fillWidth: true }
                            Text { text: "Modo"; color: Theme.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 54; horizontalAlignment: Text.AlignRight }
                            Text { text: "Score"; color: Theme.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 60; horizontalAlignment: Text.AlignRight }
                            Text { text: "t/s"; color: Theme.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 60; horizontalAlignment: Text.AlignRight }
                            Text { text: "TTFT"; color: Theme.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 60; horizontalAlignment: Text.AlignRight }
                            Text { text: "RAM"; color: Theme.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 60; horizontalAlignment: Text.AlignRight }
                            Text { text: "VRAM"; color: Theme.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 60; horizontalAlignment: Text.AlignRight }
                            Text { text: "Fecha"; color: Theme.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                    // Filas de resultados
                    ListView {
                        id: resultsList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: App.benchmarkResults
                        spacing: 2
                        ScrollBar.vertical: ScrollBar {}

                        // Placeholder vacío
                        Text {
                            anchors.centerIn: parent
                            visible: resultsList.count === 0 && !App.benchmarkRunning
                            text: "Sin resultados — seleccioná perfiles y ejecutá benchmark"
                            color: Theme.textMuted; font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                        }

                        // Spinner durante benchmark sin resultados aún
                        ColumnLayout {
                            anchors.centerIn: parent
                            visible: resultsList.count === 0 && App.benchmarkRunning
                            spacing: 12

                            BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: App.benchmarkRunning }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: App.benchmarkStatus
                                color: Theme.textSecondary; font.pixelSize: 13
                            }
                        }

                        delegate: Rectangle {
                            id: resultRow
                            width: resultsList.width
                            height: expanded ? expandedHeight + 36 : 36
                            color: rowHover.containsMouse ? Theme.hoverBg : "transparent"
                            radius: 4
                            clip: true

                            property bool expanded: false
                            property int expandedHeight: taskList.implicitHeight + 8

                            Behavior on height { NumberAnimation { duration: 150 } }

                            HoverHandler { id: rowHover }

                            // ── Main row ───────────────────────────────────
                            RowLayout {
                                anchors { left: parent.left; right: parent.right; leftMargin: 12; rightMargin: 12 }
                                height: 36
                                spacing: 0

                                Text {
                                    text: modelData.profileName ?? ""
                                    color: Theme.textPrimary; font.pixelSize: 12
                                    elide: Text.ElideRight; Layout.fillWidth: true
                                }
                                Text {
                                    text: (modelData.mode ?? "").toUpperCase()
                                    color: Theme.textMuted; font.pixelSize: 11
                                    Layout.preferredWidth: 54; horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: {
                                        const s = modelData.qualityScore ?? 0
                                        const t = modelData.qualityTotal ?? 0
                                        return t > 0 ? s + "/" + t : "—"
                                    }
                                    color: {
                                        const s = modelData.qualityScore ?? 0
                                        const t = modelData.qualityTotal ?? 1
                                        const r = s / t
                                        return r >= 0.8 ? Theme.successText : r >= 0.5 ? Theme.warnText : Theme.errorText
                                    }
                                    font.pixelSize: 12; font.bold: true
                                    Layout.preferredWidth: 60; horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: {
                                        const tps = modelData.avgTps ?? 0
                                        return tps > 0 ? tps.toFixed(1) : "—"
                                    }
                                    color: Theme.accent; font.pixelSize: 12
                                    Layout.preferredWidth: 60; horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: {
                                        const ms = modelData.avgTtftMs ?? 0
                                        return ms > 0 ? ms.toFixed(0) + " ms" : "—"
                                    }
                                    color: Theme.textSecondary; font.pixelSize: 11
                                    Layout.preferredWidth: 60; horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: {
                                        const mb = modelData.ramMb ?? 0
                                        return mb > 0 ? (mb / 1024).toFixed(1) + " GB" : "—"
                                    }
                                    color: Theme.textSecondary; font.pixelSize: 11
                                    Layout.preferredWidth: 60; horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: {
                                        const mb = modelData.vramMb ?? 0
                                        return mb > 0 ? mb.toFixed(0) + " MB" : "—"
                                    }
                                    color: Theme.textSecondary; font.pixelSize: 11
                                    Layout.preferredWidth: 60; horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: {
                                        const ts = modelData.timestamp ?? 0
                                        return ts > 0 ? new Date(ts).toLocaleDateString(Qt.locale(), "d MMM HH:mm") : ""
                                    }
                                    color: Theme.textMuted; font.pixelSize: 10
                                    Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight
                                }
                            }

                            TapHandler { onTapped: resultRow.expanded = !resultRow.expanded }

                            // ── Task breakdown (expanded) ──────────────────
                            Column {
                                id: taskList
                                anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 36; leftMargin: 24; rightMargin: 12 }
                                spacing: 3
                                visible: resultRow.expanded

                                Repeater {
                                    model: modelData.tasks ?? []
                                    delegate: RowLayout {
                                        width: taskList.width
                                        spacing: 8

                                        Text {
                                            text: modelData.type === "speed" ? "⚡" : (modelData.passed ? "✓" : "✗")
                                            color: modelData.type === "speed" ? Theme.accent
                                                 : (modelData.passed ? Theme.successText : Theme.errorText)
                                            font.pixelSize: 11
                                            Layout.preferredWidth: 16
                                        }
                                        Text {
                                            text: modelData.id ?? ""
                                            color: Theme.textSecondary; font.pixelSize: 11
                                            Layout.preferredWidth: 160
                                        }
                                        Text {
                                            text: modelData.type === "speed"
                                                ? (modelData.tps ?? 0).toFixed(1) + " t/s  TTFT " + (modelData.ttft_ms ?? 0).toFixed(0) + " ms"
                                                : (modelData.passed ? "correcto" : "incorrecto") + "  " + (modelData.elapsed_ms ?? 0) + " ms"
                                            color: Theme.textMuted; font.pixelSize: 10
                                            Layout.fillWidth: true
                                        }
                                    }
                                }

                                Item { height: 4 }
                            }
                        }
                    }
                }
            }
        }
    }
}
