import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Item {
    id: root

    property var selectedIds: []
    property string sortColumn: ""
    property int sortDirection: 0
    property var failureRow: ({})
    property string modeFilter: ""
    property string benchmarkFilter: ""
    property int leftPanelWidth: 280
    property int leftPanelMinWidth: 240
    property int leftPanelMaxWidth: Math.max(leftPanelMinWidth, Math.min(560, Math.max(leftPanelMinWidth, width - 520)))

    onLeftPanelMaxWidthChanged: leftPanelWidth = clampLeftPanelWidth(leftPanelWidth)

    function cycleSort(column) {
        if (sortColumn !== column) {
            sortColumn = column
            sortDirection = 1
        } else if (sortDirection === 1) {
            sortDirection = -1
        } else {
            sortColumn = ""
            sortDirection = 0
        }
    }
    function sortIndicator(column) {
        if (sortColumn !== column || sortDirection === 0) return "↕"
        return sortDirection > 0 ? "▲" : "▼"
    }
    function benchmarkTargetLabel(row) {
        const target = (row.target ?? "").toString().toLowerCase()
        return target === "agent" ? "Agente" : "Chat"
    }
    function benchmarkNameLabel(row) {
        const explicitName = (row.benchmarkName ?? "").toString()
        if (explicitName.length > 0) return explicitName
        const mode = (row.mode ?? "").toString().toLowerCase()
        if (mode === "short") return "Corta"
        if (mode === "full") return "Completa"
        const label = (row.runLabel ?? "").toString()
        return label.length > 0 && label !== "standard" ? label : mode.toUpperCase()
    }
    function sortValue(row, column) {
        if (column === "profile") return (row.profileName ?? "").toString().toLowerCase()
        if (column === "target") return benchmarkTargetLabel(row).toLowerCase()
        if (column === "benchmark") return benchmarkNameLabel(row).toLowerCase()
        if (column === "score") {
            const total = row.qualityTotal ?? 0
            return total > 0 ? (row.qualityScore ?? 0) / total : -1
        }
        if (column === "firstAttemptScore") {
            const total = row.firstAttemptTotal ?? row.qualityTotal ?? 0
            return total > 0 ? (row.firstAttemptScore ?? row.qualityScore ?? 0) / total : -1
        }
        if (column === "finalScore") {
            const total = row.finalTotal ?? row.qualityTotal ?? 0
            return total > 0 ? (row.finalScore ?? row.qualityScore ?? 0) / total : -1
        }
        if (column === "repairAttempts") return row.repairAttempts ?? 0
        if (column === "timeToFirstAttempt") return row.timeToFirstAttempt ?? row.elapsedSec ?? 0
        if (column === "totalTime") return row.totalTime ?? row.elapsedSec ?? 0
        if (column === "passedAfterRepair") return (row.passedAfterRepair ?? false) ? 1 : 0
        if (column === "tps") return row.avgTps ?? 0
        if (column === "ttft") return row.avgTtftMs ?? 0
        if (column === "seconds") return row.elapsedSec ?? 0
        if (column === "ram") return row.ramMb ?? 0
        if (column === "vram") return row.vramMb ?? 0
        if (column === "date") return row.timestamp ?? 0
        return ""
    }
    function sortedBenchmarkResults(rows, column, direction) {
        const copy = []
        for (let i = 0; i < rows.length; i++)
            copy.push({ row: rows[i], originalIndex: i })
        if (direction === 0 || column === "")
            return copy.map(x => x.row)
        copy.sort((a, b) => {
            const av = sortValue(a.row, column)
            const bv = sortValue(b.row, column)
            let cmp = 0
            if (typeof av === "number" && typeof bv === "number")
                cmp = av === bv ? 0 : (av < bv ? -1 : 1)
            else
                cmp = av.toString().localeCompare(bv.toString())
            if (cmp === 0) cmp = a.originalIndex - b.originalIndex
            return direction > 0 ? cmp : -cmp
        })
        return copy.map(x => x.row)
    }
    function uniqueColumnValues(rows, getter) {
        const seen = {}
        const out = []
        for (let i = 0; i < rows.length; i++) {
            const value = getter(rows[i])
            if (value.length === 0 || seen[value]) continue
            seen[value] = true
            out.push(value)
        }
        out.sort((a, b) => a.localeCompare(b))
        return out
    }
    function modeFilterValues() {
        return ["Todos"].concat(uniqueColumnValues(App.benchmarkResults, row => benchmarkTargetLabel(row)))
    }
    function benchmarkFilterValues() {
        return ["Todos"].concat(uniqueColumnValues(App.benchmarkResults, row => benchmarkNameLabel(row)))
    }
    function indexOfValue(values, value) {
        for (let i = 0; i < values.length; i++)
            if (values[i] === value) return i
        return 0
    }
    function filteredBenchmarkResults(rows) {
        const out = []
        for (let i = 0; i < rows.length; i++) {
            const row = rows[i]
            if (modeFilter !== "" && benchmarkTargetLabel(row) !== modeFilter) continue
            if (benchmarkFilter !== "" && benchmarkNameLabel(row) !== benchmarkFilter) continue
            out.push(row)
        }
        return out
    }
    function visibleBenchmarkResults() {
        return sortedBenchmarkResults(filteredBenchmarkResults(App.benchmarkResults), sortColumn, sortDirection)
    }
    function scoreLabel(row, scoreKey, totalKey) {
        const s = row[scoreKey] ?? row.qualityScore ?? 0
        const t = row[totalKey] ?? row.qualityTotal ?? 0
        return t > 0 ? s + "/" + t : "—"
    }
    function scoreColor(row, scoreKey, totalKey) {
        const s = row[scoreKey] ?? row.qualityScore ?? 0
        const t = row[totalKey] ?? row.qualityTotal ?? 1
        const r = s / t
        return r >= 0.8 ? Theme.successText : r >= 0.5 ? Theme.warnText : Theme.errorText
    }
    function secondsLabel(value) {
        const sec = value ?? 0
        return sec > 0 ? sec.toFixed(1) + " s" : "—"
    }
    function clampLeftPanelWidth(value) {
        return Math.max(leftPanelMinWidth, Math.min(leftPanelMaxWidth, Math.round(value)))
    }

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

    Component {
        id: sortableHeader
        Item {
            property string title: ""
            property string column: ""
            property int columnWidth: 60
            property bool fill: false
            Layout.fillWidth: fill
            Layout.preferredWidth: fill ? 1 : columnWidth
            height: 30

            Text {
                anchors.fill: parent
                text: title + " " + root.sortIndicator(column)
                color: headerHover.hovered || root.sortColumn === column ? Theme.textPrimary : Theme.textSecondary
                font.pixelSize: 11
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: fill ? Text.AlignLeft : Text.AlignRight
            }
            HoverHandler { id: headerHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.cycleSort(column) }
        }
    }

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
                Layout.preferredWidth: root.leftPanelWidth
                Layout.minimumWidth: root.leftPanelMinWidth
                Layout.maximumWidth: root.leftPanelMaxWidth
                Layout.fillHeight: true
                color: Theme.surfaceBg

                ColumnLayout {
                    anchors { fill: parent; margins: 16 }
                    spacing: 14

                    Text {
                        text: "OBJETIVO"
                        color: Theme.textSecondary
                        font.pixelSize: 10; font.bold: true
                    }
                    ColumnLayout {
                        ButtonGroup { id: targetGroup }
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            TapHandler { onTapped: modelTarget.checked = true }
                            RadioButton {
                                id: modelTarget
                                text: ""
                                checked: true
                                ButtonGroup.group: targetGroup
                                padding: 0
                                leftPadding: 0
                                rightPadding: 0
                                Layout.minimumWidth: implicitIndicatorWidth
                                Layout.preferredWidth: implicitIndicatorWidth
                                Layout.maximumWidth: implicitIndicatorWidth
                            }
                            Text {
                                text: "Modo Chat"
                                color: Theme.theme === "oled" ? "white" : Theme.textPrimary
                                font.pixelSize: 12
                            }
                        }
                        Item { height: 2 }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            TapHandler { onTapped: agentTarget.checked = true }
                            RadioButton {
                                id: agentTarget
                                text: ""
                                ButtonGroup.group: targetGroup
                                padding: 0
                                leftPadding: 0
                                rightPadding: 0
                                Layout.minimumWidth: implicitIndicatorWidth
                                Layout.preferredWidth: implicitIndicatorWidth
                                Layout.maximumWidth: implicitIndicatorWidth
                            }
                            Text {
                                text: "Modo Agente"
                                color: Theme.theme === "oled" ? "white" : Theme.textPrimary
                                font.pixelSize: 12
                            }
                        }
                    }

                    CheckBox {
                        id: benchmarkThinkingCheck
                        text: "Thinking"
                        checked: App.thinkingEnabled
                        onToggled: App.thinkingEnabled = checked
                        contentItem: Text {
                            text: benchmarkThinkingCheck.text
                            color: Theme.theme === "oled" ? "white" : Theme.textPrimary
                            font.pixelSize: 12
                            leftPadding: benchmarkThinkingCheck.indicator.width + 6
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Text {
                        text: "MODO DE PRUEBA"
                        color: Theme.textSecondary
                        font.pixelSize: 10; font.bold: true
                    }

                    ColumnLayout {
                        ButtonGroup { id: modeGroup }
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            TapHandler { onTapped: shortMode.checked = true }
                            RadioButton {
                                id: shortMode
                                text: ""
                                checked: true
                                ButtonGroup.group: modeGroup
                                padding: 0
                                leftPadding: 0
                                rightPadding: 0
                                Layout.minimumWidth: implicitIndicatorWidth
                                Layout.preferredWidth: implicitIndicatorWidth
                                Layout.maximumWidth: implicitIndicatorWidth
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "Corta (~2 min) 2 speed + 5 quality tasks"
                                color: Theme.textSecondary
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                        }
                        Item { height: 2 }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            TapHandler { onTapped: fullMode.checked = true }
                            RadioButton {
                                id: fullMode
                                text: ""
                                ButtonGroup.group: modeGroup
                                padding: 0
                                leftPadding: 0
                                rightPadding: 0
                                Layout.minimumWidth: implicitIndicatorWidth
                                Layout.preferredWidth: implicitIndicatorWidth
                                Layout.maximumWidth: implicitIndicatorWidth
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "Completa (~5 min) 5 speed + 7 quality tasks"
                                color: Theme.textSecondary
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                        }
                        Item { height: 2 }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            TapHandler { onTapped: customMode.checked = true }
                            RadioButton {
                                id: customMode
                                text: ""
                                ButtonGroup.group: modeGroup
                                padding: 0
                                leftPadding: 0
                                rightPadding: 0
                                Layout.minimumWidth: implicitIndicatorWidth
                                Layout.preferredWidth: implicitIndicatorWidth
                                Layout.maximumWidth: implicitIndicatorWidth
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "Custom benchmark Tus prompts personalizados"
                                color: Theme.textSecondary
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                        }
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
                                        id: profileNameText
                                        text: model.name || "(sin nombre)"
                                        color: Theme.textPrimary; font.pixelSize: 12
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: profileList.width - 60; elide: Text.ElideRight
                                        ToolTip.visible: pdHover.hovered
                                        ToolTip.delay: 350
                                        ToolTip.timeout: 5000
                                        ToolTip.text: text
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
                                        App.startCustomBenchmark(root.selectedIds, root.customId, passesSpin.value,
                                                                 agentTarget.checked ? "agent" : "model")
                                } else {
                                    App.startBenchmark(root.selectedIds, shortMode.checked ? "short" : "full", passesSpin.value,
                                                       agentTarget.checked ? "agent" : "model")
                                }
                            }
                        }

                    }
                }
            }

            Item {
                id: leftPanelResizeHandle
                Layout.preferredWidth: 10
                Layout.fillHeight: true

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: resizeMouse.pressed || resizeHover.hovered ? 3 : 1
                    height: parent.height
                    color: resizeMouse.pressed || resizeHover.hovered ? Theme.accent : Theme.divider
                }

                HoverHandler {
                    id: resizeHover
                    cursorShape: Qt.SplitHCursor
                }

                MouseArea {
                    id: resizeMouse
                    anchors.fill: parent
                    cursorShape: Qt.SplitHCursor
                    property real pressRootX: 0
                    property int pressWidth: root.leftPanelWidth

                    onPressed: function(mouse) {
                        pressRootX = mapToItem(root, mouse.x, mouse.y).x
                        pressWidth = root.leftPanelWidth
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        const currentRootX = mapToItem(root, mouse.x, mouse.y).x
                        root.leftPanelWidth = root.clampLeftPanelWidth(pressWidth + currentRootX - pressRootX)
                    }
                }
            }

            // ── Panel derecho: resultados ──────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.baseBg
                clip: true

                ColumnLayout {
                    anchors { fill: parent; margins: 20 }
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: "Filtros"
                            color: Theme.textSecondary
                            font.pixelSize: 11
                            font.bold: true
                        }
                        ComboBox {
                            id: modeFilterCombo
                            Layout.preferredWidth: 150
                            model: root.modeFilterValues()
                            currentIndex: root.indexOfValue(model, root.modeFilter.length > 0 ? root.modeFilter : "Todos")
                            onActivated: root.modeFilter = currentText === "Todos" ? "" : currentText
                        }
                        ComboBox {
                            id: benchmarkFilterCombo
                            Layout.preferredWidth: 230
                            model: root.benchmarkFilterValues()
                            currentIndex: root.indexOfValue(model, root.benchmarkFilter.length > 0 ? root.benchmarkFilter : "Todos")
                            onActivated: root.benchmarkFilter = currentText === "Todos" ? "" : currentText
                        }
                        LcButton {
                            text: "Limpiar"
                            secondary: true
                            enabled: root.modeFilter.length > 0 || root.benchmarkFilter.length > 0
                            onClicked: {
                                root.modeFilter = ""
                                root.benchmarkFilter = ""
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: resultsList.count + " resultados"
                            color: Theme.textMuted
                            font.pixelSize: 11
                        }
                    }

                    // Cabecera tabla
                    Rectangle {
                        Layout.fillWidth: true
                        height: 30
                        color: Theme.surfaceBg; radius: 6

                        RowLayout {
                            anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                            spacing: 0
                            Loader { sourceComponent: sortableHeader; Layout.fillWidth: true; onLoaded: { item.title = "Perfil"; item.column = "profile"; item.fill = true } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: 58; onLoaded: { item.title = "Modo"; item.column = "target"; item.columnWidth = 58 } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: 100; onLoaded: { item.title = "Benchmark"; item.column = "benchmark"; item.columnWidth = 100 } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: 60; onLoaded: { item.title = "Score"; item.column = "score"; item.columnWidth = 60 } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: 58; onLoaded: { item.title = "First"; item.column = "firstAttemptScore"; item.columnWidth = 58 } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: 58; onLoaded: { item.title = "Final"; item.column = "finalScore"; item.columnWidth = 58 } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: 52; onLoaded: { item.title = "Fixes"; item.column = "repairAttempts"; item.columnWidth = 52 } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: 62; onLoaded: { item.title = "T First"; item.column = "timeToFirstAttempt"; item.columnWidth = 62 } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: 62; onLoaded: { item.title = "T Total"; item.column = "totalTime"; item.columnWidth = 62 } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: 68; onLoaded: { item.title = "Repaired"; item.column = "passedAfterRepair"; item.columnWidth = 68 } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: 60; onLoaded: { item.title = "TPS"; item.column = "tps"; item.columnWidth = 60 } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: 60; onLoaded: { item.title = "TTFT"; item.column = "ttft"; item.columnWidth = 60 } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: 70; onLoaded: { item.title = "Segundos"; item.column = "seconds"; item.columnWidth = 70 } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: 60; onLoaded: { item.title = "RAM"; item.column = "ram"; item.columnWidth = 60 } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: 60; onLoaded: { item.title = "VRAM"; item.column = "vram"; item.columnWidth = 60 } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: 118; onLoaded: { item.title = "Fecha"; item.column = "date"; item.columnWidth = 118 } }
                            Item { Layout.preferredWidth: 34; height: 30 }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                    // Filas de resultados
                    ListView {
                        id: resultsList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.visibleBenchmarkResults()
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
                            property bool failed: modelData.failed ?? false
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
                                    text: root.benchmarkTargetLabel(modelData)
                                    color: Theme.textMuted; font.pixelSize: 11
                                    Layout.preferredWidth: 58; horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: root.benchmarkNameLabel(modelData)
                                    color: Theme.textSecondary; font.pixelSize: 11
                                    elide: Text.ElideRight
                                    Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight
                                }
                                Item {
                                    Layout.preferredWidth: 60
                                    height: 36
                                    Text {
                                        anchors.fill: parent
                                        visible: !resultRow.failed
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
                                        horizontalAlignment: Text.AlignRight
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    LcButton {
                                        anchors { verticalCenter: parent.verticalCenter; right: parent.right }
                                        width: 52; height: 24
                                        text: "Fallo"
                                        danger: true
                                        visible: resultRow.failed
                                        onClicked: {
                                            root.failureRow = modelData
                                            failureDialog.open()
                                        }
                                    }
                                }
                                Text {
                                    text: root.scoreLabel(modelData, "firstAttemptScore", "firstAttemptTotal")
                                    color: root.scoreColor(modelData, "firstAttemptScore", "firstAttemptTotal")
                                    font.pixelSize: 11
                                    font.bold: true
                                    Layout.preferredWidth: 58; horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: root.scoreLabel(modelData, "finalScore", "finalTotal")
                                    color: root.scoreColor(modelData, "finalScore", "finalTotal")
                                    font.pixelSize: 11
                                    font.bold: true
                                    Layout.preferredWidth: 58; horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: (modelData.repairAttempts ?? 0).toString()
                                    color: (modelData.repairAttempts ?? 0) > 0 ? Theme.warnText : Theme.textMuted
                                    font.pixelSize: 11
                                    Layout.preferredWidth: 52; horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: root.secondsLabel(modelData.timeToFirstAttempt ?? modelData.elapsedSec)
                                    color: Theme.textSecondary; font.pixelSize: 10
                                    Layout.preferredWidth: 62; horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: root.secondsLabel(modelData.totalTime ?? modelData.elapsedSec)
                                    color: Theme.textSecondary; font.pixelSize: 10
                                    Layout.preferredWidth: 62; horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: (modelData.passedAfterRepair ?? false) ? "Sí" : "No"
                                    color: (modelData.passedAfterRepair ?? false) ? Theme.successText : Theme.textMuted
                                    font.pixelSize: 11
                                    Layout.preferredWidth: 68; horizontalAlignment: Text.AlignRight
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
                                        const sec = modelData.elapsedSec ?? 0
                                        return sec > 0 ? sec.toFixed(1) + " s" : "—"
                                    }
                                    color: Theme.textSecondary; font.pixelSize: 11
                                    Layout.preferredWidth: 70; horizontalAlignment: Text.AlignRight
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
                                        return ts > 0 ? Qt.formatDateTime(new Date(ts), "d MMM HH:mm") : ""
                                    }
                                    color: Theme.textMuted; font.pixelSize: 10
                                    Layout.preferredWidth: 118; horizontalAlignment: Text.AlignRight
                                }
                                Item {
                                    Layout.preferredWidth: 34
                                    height: 36
                                    Rectangle {
                                        id: delBox
                                        width: 24; height: 24; radius: 4
                                        anchors { verticalCenter: parent.verticalCenter; right: parent.right }
                                        color: delHover.containsMouse ? Theme.errorText : "transparent"
                                        border.color: delHover.containsMouse ? Theme.errorText : Theme.divider
                                        border.width: 1
                                        visible: rowHover.containsMouse || delHover.containsMouse
                                        Text {
                                            anchors.centerIn: parent
                                            text: "✕"; font.pixelSize: 12
                                            color: delHover.containsMouse ? "white" : Theme.textMuted
                                        }
                                        HoverHandler { id: delHover }
                                        TapHandler { onTapped: App.removeBenchmarkResultById(modelData.id ?? "") }
                                    }
                                }
                            }

                            TapHandler { onTapped: resultRow.expanded = !resultRow.expanded }

                            // ── Task breakdown (expanded) ──────────────────
                            Column {
                                id: taskList
                                anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 36; leftMargin: 24; rightMargin: 12 }
                                spacing: 3
                                visible: resultRow.expanded

                                Rectangle {
                                    visible: resultRow.failed
                                    width: taskList.width
                                    height: failureSummary.implicitHeight + 14
                                    radius: 6
                                    color: Theme.errorBg
                                    border.color: Theme.errorText
                                    border.width: 1

                                    RowLayout {
                                        anchors { fill: parent; margins: 8 }
                                        spacing: 8
                                        Text {
                                            id: failureSummary
                                            text: modelData.failureMessage ?? "Falló la pasada."
                                            color: Theme.errorText
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        LcButton {
                                            text: "Ver fallo"
                                            danger: true
                                            Layout.preferredHeight: 26
                                            onClicked: {
                                                root.failureRow = modelData
                                                failureDialog.open()
                                            }
                                        }
                                    }
                                }

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

                                Column {
                                    width: taskList.width
                                    spacing: 3
                                    visible: (modelData.acceptance ?? []).length > 0

                                    Text {
                                        text: "Criterios de aceptación"
                                        color: Theme.textSecondary
                                        font.pixelSize: 10
                                        font.bold: true
                                    }
                                    Repeater {
                                        model: modelData.acceptance ?? []
                                        delegate: Column {
                                            width: taskList.width
                                            spacing: 1
                                            RowLayout {
                                                width: parent.width
                                                spacing: 8
                                                Text {
                                                    text: modelData.passed ? "✓" : "✗"
                                                    color: modelData.passed ? Theme.successText : Theme.errorText
                                                    font.pixelSize: 11
                                                    Layout.preferredWidth: 16
                                                }
                                                Text {
                                                    text: (modelData.type ?? "") + " · " + (modelData.name ?? modelData.command ?? "")
                                                    color: Theme.textSecondary
                                                    font.pixelSize: 11
                                                    elide: Text.ElideRight
                                                    Layout.fillWidth: true
                                                }
                                                Text {
                                                    text: modelData.exitCode !== undefined ? ("exit " + modelData.exitCode) : ""
                                                    color: Theme.textMuted
                                                    font.pixelSize: 10
                                                    Layout.preferredWidth: 54
                                                    horizontalAlignment: Text.AlignRight
                                                }
                                            }
                                            Text {
                                                visible: (modelData.output ?? "").length > 0
                                                width: parent.width
                                                leftPadding: 24
                                                text: modelData.output ?? ""
                                                color: modelData.passed ? Theme.textMuted : Theme.errorText
                                                font.family: "Consolas"
                                                font.pixelSize: 10
                                                wrapMode: Text.Wrap
                                                maximumLineCount: 4
                                                elide: Text.ElideRight
                                            }
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

    Dialog {
        id: failureDialog
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(parent ? parent.width - 80 : 900, 900)
        height: Math.min(parent ? parent.height - 80 : 680, 680)
        title: "Fallo de benchmark"

        background: Rectangle {
            color: Theme.cardBg
            radius: 8
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 10

            Text {
                Layout.fillWidth: true
                text: root.failureRow.profileName ?? ""
                color: Theme.textPrimary
                font.pixelSize: 14
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: {
                    const stage = root.failureRow.failureStage ?? ""
                    const msg = root.failureRow.failureMessage ?? "Falló la pasada."
                    return stage.length > 0 ? stage + ": " + msg : msg
                }
                color: Theme.errorText
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.inputBg
                radius: 6
                border.color: Theme.borderColor
                clip: true

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 8
                    TextArea {
                        readOnly: true
                        wrapMode: TextArea.Wrap
                        text: root.failureRow.failureDetail ?? ""
                        color: Theme.textSecondary
                        font.family: "Consolas"
                        font.pixelSize: 11
                        background: null
                        selectByMouse: true
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                LcButton {
                    text: "Copiar"
                    secondary: true
                    onClicked: App.copyToClipboard((root.failureRow.failureDetail ?? "").toString())
                }
                LcButton {
                    text: "Cerrar"
                    onClicked: failureDialog.close()
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
                for (let i = 0; i < ps.length; i++) {
                    const acc = ps[i].acceptance || {}
                    const files = acc.files || []
                    const commands = acc.commands || []
                    const cmdLines = []
                    for (let c = 0; c < commands.length; c++)
                        cmdLines.push(commands[c].command || "")
                    taskModel.append({
                        prompt: ps[i].prompt || "",
                        acceptanceFiles: files.join("\n"),
                        acceptanceCommands: cmdLines.join("\n")
                    })
                }
            } else {
                editId = ""
                nameField.text = ""
            }
            if (taskModel.count === 0)
                taskModel.append({ prompt: "", acceptanceFiles: "", acceptanceCommands: "" })
        }

        function save() {
            const prompts = []
            for (let i = 0; i < taskModel.count; i++) {
                const it = taskModel.get(i)
                if ((it.prompt || "").trim() === "") continue
                const files = []
                const rawFiles = (it.acceptanceFiles || "").split(/\r?\n/)
                for (let f = 0; f < rawFiles.length; f++) {
                    const line = rawFiles[f].trim()
                    if (line.length > 0) files.push(line)
                }
                const commands = []
                const rawCommands = (it.acceptanceCommands || "").split(/\r?\n/)
                for (let c = 0; c < rawCommands.length; c++) {
                    const cmd = rawCommands[c].trim()
                    if (cmd.length > 0)
                        commands.push({ name: "cmd_" + (c + 1), command: cmd, timeoutMs: 30000 })
                }
                const promptDef = { id: "task_" + (i + 1), prompt: it.prompt, isSpeed: true, maxTokens: 8192 }
                if (files.length > 0 || commands.length > 0)
                    promptDef.acceptance = { files: files, commands: commands }
                prompts.push(promptDef)
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

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6

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
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    TextArea {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 56
                                        wrapMode: TextArea.Wrap
                                        placeholderText: "Archivos esperados, uno por línea"
                                        color: Theme.textSecondary
                                        placeholderTextColor: Theme.textMuted
                                        font.pixelSize: 11
                                        text: model.acceptanceFiles || ""
                                        onTextChanged: taskModel.setProperty(index, "acceptanceFiles", text)
                                        background: Rectangle { color: "transparent"; border.color: Theme.divider; radius: 4 }
                                    }
                                    TextArea {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 56
                                        wrapMode: TextArea.Wrap
                                        placeholderText: "Comandos de aceptación, uno por línea"
                                        color: Theme.textSecondary
                                        placeholderTextColor: Theme.textMuted
                                        font.pixelSize: 11
                                        text: model.acceptanceCommands || ""
                                        onTextChanged: taskModel.setProperty(index, "acceptanceCommands", text)
                                        background: Rectangle { color: "transparent"; border.color: Theme.divider; radius: 4 }
                                    }
                                }
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
                        onClicked: taskModel.append({ prompt: "", acceptanceFiles: "", acceptanceCommands: "" })
                    }
                    Item { Layout.fillWidth: true }
                    LcButton { text: "Cancelar"; secondary: true; onClicked: editor.close() }
                    LcButton { text: "Guardar"; onClicked: editor.save() }
                }
            }
        }
    }
}
