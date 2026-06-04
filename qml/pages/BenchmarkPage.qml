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

    // Custom benchmark selection: "" = standard tasks, else a custom benchmark id
    property string customId: ""

    Component.onCompleted: {
        if (App.loadBenchmarkResults) App.loadBenchmarkResults()
        if (App.loadCustomBenchmarks) App.loadCustomBenchmarks()
    }

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
                        Item { height: 2 }
                        RadioButton { id: customMode; text: "Custom benchmark" }
                        Text { text: "Tus prompts personalizados"; color: Theme.textMuted; font.pixelSize: 11; leftPadding: 24 }
                    }

                    // ── Selector de benchmark personalizado (solo en modo custom) ──
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: customMode.checked

                        ComboBox {
                            id: benchCombo
                            Layout.fillWidth: true
                            textRole: "text"
                            valueRole: "id"
                            model: {
                                const arr = []
                                const cs = App.customBenchmarks || []
                                for (let i = 0; i < cs.length; i++)
                                    arr.push({ id: cs[i].id, text: cs[i].name || "(sin nombre)" })
                                if (arr.length === 0)
                                    arr.push({ id: "", text: "(sin benchmarks — creá uno)" })
                                return arr
                            }
                            onActivated: root.customId = currentValue
                            onModelChanged: { currentIndex = Math.max(0, indexOfValue(root.customId)); root.customId = currentValue }
                            Component.onCompleted: { currentIndex = Math.max(0, indexOfValue(root.customId)); root.customId = currentValue }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            LcButton {
                                Layout.fillWidth: true
                                text: "Nuevo"
                                onClicked: { editor.loadDef(null); editor.open() }
                            }
                            LcButton {
                                Layout.fillWidth: true
                                text: "Editar"
                                enabled: root.customId !== ""
                                onClicked: {
                                    const cs = App.customBenchmarks || []
                                    for (let i = 0; i < cs.length; i++)
                                        if (cs[i].id === root.customId) { editor.loadDef(cs[i]); editor.open(); break }
                                }
                            }
                            LcButton {
                                Layout.fillWidth: true
                                text: "Borrar"
                                danger: true
                                enabled: root.customId !== ""
                                onClicked: {
                                    App.deleteCustomBenchmark(root.customId)
                                    root.customId = ""
                                    benchCombo.currentIndex = 0
                                }
                            }
                        }
                    }

                    Rectangle { height: 1; Layout.fillWidth: true; color: Theme.divider }

                    Text {
                        text: "PERFILES A COMPARAR"
                        color: Theme.textSecondary
                        font.pixelSize: 10; font.bold: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 120
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
                                property string profileId: model.profileId ?? ""
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
                            ScrollBar.vertical: LcScrollBar {}
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

                    // Pasadas por perfil
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        enabled: !App.benchmarkRunning
                        Text {
                            text: "Pasadas por perfil"
                            color: Theme.textSecondary; font.pixelSize: 12
                            Layout.fillWidth: true
                            verticalAlignment: Text.AlignVCenter
                        }
                        SpinBox {
                            id: passesSpin
                            from: 1; to: 20; value: 1; editable: true
                            implicitWidth: 96
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        LcButton {
                            Layout.fillWidth: true
                            text: App.benchmarkRunning ? "Cancelar" : "Iniciar benchmark"
                            danger: App.benchmarkRunning
                            enabled: App.benchmarkRunning
                                     || (root.selectedIds.length > 0
                                         && (!customMode.checked || root.customId !== ""))
                            onClicked: {
                                if (App.benchmarkRunning) {
                                    App.cancelBenchmark()
                                } else if (customMode.checked) {
                                    if (root.customId !== "")
                                        App.startCustomBenchmark(root.selectedIds, root.customId, passesSpin.value)
                                } else {
                                    App.startBenchmark(root.selectedIds, shortMode.checked ? "short" : "full", passesSpin.value)
                                }
                            }
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
                        ScrollBar.vertical: LcScrollBar {}

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

                            // Click derecho → menú contextual
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: rowMenu.popup()
                            }
                            Menu {
                                id: rowMenu
                                MenuItem {
                                    text: "Ver carpeta contenedora"
                                    enabled: (modelData.runDir ?? "") !== ""
                                    onTriggered: App.openBenchmarkFolder(modelData.runDir ?? "")
                                }
                            }

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

                            // ── Botón borrar (al pasar el mouse) ───────────
                            Rectangle {
                                width: 22; height: 22; radius: 4
                                anchors { right: parent.right; rightMargin: 6; top: parent.top; topMargin: 7 }
                                color: delHover.containsMouse ? Theme.errorText : Theme.surfaceBg
                                visible: rowHover.containsMouse || delHover.containsMouse
                                Text {
                                    anchors.centerIn: parent
                                    text: "✕"; font.pixelSize: 12
                                    color: delHover.containsMouse ? "white" : Theme.textMuted
                                }
                                HoverHandler { id: delHover }
                                TapHandler { onTapped: App.removeBenchmarkResult(index) }
                            }

                            // ── Task breakdown (expanded) ──────────────────
                            Column {
                                id: taskList
                                anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 36; leftMargin: 24; rightMargin: 12 }
                                spacing: 3
                                visible: resultRow.expanded

                                Repeater {
                                    model: modelData.tasks ?? []
                                    delegate: Column {
                                        width: taskList.width
                                        spacing: 1
                                        // true cuando el task viene de un benchmark custom (heurísticas presentes)
                                        property bool isCustom: modelData.codeOnly !== undefined

                                        RowLayout {
                                            width: parent.width
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

                                        // ── Métricas extra (benchmarks custom) ──────────
                                        Text {
                                            visible: parent.isCustom
                                            leftPadding: 24
                                            color: Theme.textMuted; font.pixelSize: 10
                                            text: {
                                                const tok = modelData.tokens ?? 0
                                                const tot = ((modelData.elapsed_ms ?? 0) / 1000.0).toFixed(2)
                                                return tok + " tok · " + tot + " s total"
                                            }
                                        }
                                        Text {
                                            visible: parent.isCustom
                                            leftPadding: 24
                                            font.pixelSize: 10
                                            color: modelData.codeOnly ? Theme.successText : Theme.warnText
                                            text: (modelData.codeOnly ? "✓ solo código" : "✗ incluye prosa")
                                                + (modelData.hasCode
                                                    ? "   ·   " + (modelData.inventedDeps
                                                        ? "⚠ deps no-stdlib (" + (modelData.depCount ?? 0) + "): " + ((modelData.depList ?? []).join(", "))
                                                        : "✓ sin deps externas")
                                                    : "")
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

    // ── Editor de benchmark personalizado ──────────────────────────────────────
    Dialog {
        id: editor
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(parent ? parent.width - 80 : 860, 860)
        height: Math.min(parent ? parent.height - 60 : 720, 760)
        padding: 0
        closePolicy: Popup.CloseOnEscape

        property string editId: ""

        Overlay.modal: Rectangle { color: Theme.overlayColor }
        background: Rectangle {
            color: Theme.popupBg
            radius: 12
            border.color: Theme.popupBorderColor; border.width: 1
        }

        ListModel { id: taskModel }

        function loadDef(def) {
            taskModel.clear()
            if (def) {
                editId = def.id || ""
                nameField.text = def.name || ""
                const ps = def.prompts || []
                for (let i = 0; i < ps.length; i++)
                    taskModel.append({ prompt: ps[i].prompt || "" })
            } else {
                editId = ""
                nameField.text = ""
            }
            if (taskModel.count === 0)
                taskModel.append({ prompt: "" })
        }

        function save() {
            const prompts = []
            for (let i = 0; i < taskModel.count; i++) {
                const it = taskModel.get(i)
                if ((it.prompt || "").trim() === "") continue
                prompts.push({ id: "task_" + (i + 1), prompt: it.prompt, isSpeed: true, maxTokens: 8192 })
            }
            if (nameField.text.trim() === "" || prompts.length === 0) return
            const def = { name: nameField.text.trim(), prompts: prompts }
            if (editId !== "") def.id = editId
            const newId = App.saveCustomBenchmark(def)
            root.customId = newId
            benchCombo.currentIndex = Math.max(0, benchCombo.indexOfValue(newId))
            editor.close()
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header
            Rectangle {
                Layout.fillWidth: true
                height: 52
                color: Theme.popupHeaderBg
                radius: 12
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
                Text {
                    anchors { left: parent.left; leftMargin: 18; verticalCenter: parent.verticalCenter }
                    text: "Benchmark personalizado"
                    color: Theme.textPrimary; font.pixelSize: 14; font.bold: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 18
                spacing: 10

                LcTextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: "Nombre del benchmark"
                }

                Text {
                    text: "PROMPTS"
                    color: Theme.textSecondary; font.pixelSize: 10; font.bold: true
                }

                ListView {
                    id: taskEditList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: taskModel
                    spacing: 8
                    ScrollBar.vertical: LcScrollBar {}

                    delegate: Rectangle {
                        width: taskEditList.width - 4
                        height: rowCol.implicitHeight + 16
                        color: Theme.inputBg; radius: 6
                        border.color: promptArea.activeFocus ? Theme.inputBorderFocus : Theme.divider
                        border.width: promptArea.activeFocus ? 2 : 1

                        RowLayout {
                            id: rowCol
                            anchors { fill: parent; margins: 8 }
                            spacing: 8

                            TextArea {
                                id: promptArea
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.max(100, contentHeight + topPadding + bottomPadding)
                                wrapMode: TextArea.Wrap
                                placeholderText: "Prompt..."
                                color: Theme.textPrimary
                                placeholderTextColor: Theme.textMuted
                                font.pixelSize: 13
                                text: model.prompt
                                onTextChanged: taskModel.setProperty(index, "prompt", text)
                                background: null
                            }
                            Rectangle {
                                Layout.alignment: Qt.AlignTop
                                width: 24; height: 24; radius: 4
                                color: delTaskHover.containsMouse ? Theme.errorText : "transparent"
                                opacity: taskModel.count > 1 ? 1 : 0.3
                                Text {
                                    anchors.centerIn: parent; text: "✕"; font.pixelSize: 12
                                    color: delTaskHover.containsMouse ? "white" : Theme.textMuted
                                }
                                HoverHandler { id: delTaskHover }
                                TapHandler { onTapped: if (taskModel.count > 1) taskModel.remove(index) }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    LcButton {
                        text: "+ Prompt"
                        secondary: true
                        onClicked: taskModel.append({ prompt: "" })
                    }
                    Item { Layout.fillWidth: true }
                    LcButton { text: "Cancelar"; secondary: true; onClicked: editor.close() }
                    LcButton { text: "Guardar"; onClicked: editor.save() }
                }
            }
        }
    }
}
