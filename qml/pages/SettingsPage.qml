import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: (App.langV, App.l("settings.title"))
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: scroll.availableWidth

            Item {
                width: scroll.availableWidth
                implicitHeight: col.implicitHeight + 48

                ColumnLayout {
                    id: col
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    anchors.margins: 24
                    spacing: 28

                    // ── Appearance ───────────────────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: (App.langV, App.l("settings.appearance")).toUpperCase()
                            color: Theme.accent
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: themeInner.implicitHeight + 32

                            ColumnLayout {
                                id: themeInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 14

                                Text {
                                    text: (App.langV, App.l("settings.theme"))
                                    color: Theme.textPrimary
                                    font.pixelSize: 14
                                    font.bold: true
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Repeater {
                                        model: [
                                            { key: "dark",  labelKey: "settings.dark",  bg: "#1e1e2e", dotColor: "#313244" },
                                            { key: "light", labelKey: "settings.light", bg: "#eff1f5", dotColor: "#ccd0da" },
                                            { key: "oled",  labelKey: "settings.oled",  bg: "#000000", dotColor: "#111111" },
                                        ]

                                        delegate: Rectangle {
                                            Layout.fillWidth: true
                                            height: 70
                                            radius: 8
                                            color: modelData.bg
                                            border.color: Theme.theme === modelData.key ? Theme.accent : Theme.divider
                                            border.width: Theme.theme === modelData.key ? 2 : 1

                                            Rectangle {
                                                visible: Theme.theme === modelData.key
                                                anchors { top: parent.top; right: parent.right; margins: 6 }
                                                width: 18; height: 18; radius: 9
                                                color: Theme.accent
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "✓"
                                                    color: "#000000"
                                                    font.pixelSize: 10
                                                    font.bold: true
                                                }
                                            }

                                            ColumnLayout {
                                                anchors.centerIn: parent
                                                spacing: 6

                                                Row {
                                                    Layout.alignment: Qt.AlignHCenter
                                                    spacing: 4
                                                    Repeater {
                                                        model: 3
                                                        Rectangle {
                                                            width: 7; height: 7; radius: 4
                                                            color: modelData.dotColor
                                                        }
                                                    }
                                                }

                                                Text {
                                                    Layout.alignment: Qt.AlignHCenter
                                                    text: (App.langV, App.l(modelData.labelKey))
                                                    color: modelData.key === "light" ? "#4c4f69" : "#cdd6f4"
                                                    font.pixelSize: 12
                                                    font.bold: Theme.theme === modelData.key
                                                }
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: Theme.theme = modelData.key
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── Language ─────────────────────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: (App.langV, App.l("settings.language")).toUpperCase()
                            color: Theme.accent
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: langInner.implicitHeight + 32

                            ColumnLayout {
                                id: langInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 14

                                Text {
                                    text: (App.langV, App.l("settings.language"))
                                    color: Theme.textPrimary
                                    font.pixelSize: 14
                                    font.bold: true
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 3
                                    rowSpacing: 8
                                    columnSpacing: 8

                                    Repeater {
                                        model: [
                                            { code: "es", label: "Español",  flag: "🇦🇷" },
                                            { code: "en", label: "English",  flag: "🇺🇸" },
                                            { code: "zh", label: "中文",       flag: "🇨🇳" },
                                            { code: "fr", label: "Français", flag: "🇫🇷" },
                                            { code: "it", label: "Italiano", flag: "🇮🇹" },
                                            { code: "de", label: "Deutsch",  flag: "🇩🇪" },
                                        ]

                                        delegate: Rectangle {
                                            Layout.fillWidth: true
                                            height: 48
                                            radius: 8
                                            color: App.language === modelData.code ? Theme.accent : Theme.inputBg
                                            border.color: App.language === modelData.code ? Theme.accent : Theme.borderColor
                                            border.width: 1

                                            Row {
                                                anchors.centerIn: parent
                                                spacing: 8
                                                Text { text: modelData.flag; font.pixelSize: 20 }
                                                Text {
                                                    text: modelData.label
                                                    color: App.language === modelData.code
                                                        ? Theme.btnPrimaryText : Theme.textPrimary
                                                    font.pixelSize: 13
                                                    font.bold: App.language === modelData.code
                                                    anchors.verticalCenter: parent.verticalCenter
                                                }
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: App.language = modelData.code
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── Integrations ─────────────────────────────────────────
                    ColumnLayout {
                        id: intgSection
                        Layout.fillWidth: true
                        spacing: 10

                        property var items: []
                        property string testMsgId: ""
                        property string testMsg: ""
                        property bool testOk: false
                        function reload() { items = App.integrations() }
                        Component.onCompleted: reload()

                        Connections {
                            target: App
                            function onIntegrationsChanged() { intgSection.reload() }
                            function onIntegrationTestResult(id, ok, message) {
                                intgSection.testMsgId = id; intgSection.testOk = ok; intgSection.testMsg = message
                            }
                        }

                        Text {
                            text: "INTEGRATIONS"
                            color: Theme.accent
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: intgInner.implicitHeight + 32

                            ColumnLayout {
                                id: intgInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 14

                                Text {
                                    text: "Conexiones a servicios externos en un solo lugar (MCP + API)."
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }

                                // Lista de integraciones
                                Text {
                                    visible: intgSection.items.length === 0
                                    text: "Sin integraciones configuradas."
                                    color: Theme.textMuted
                                    font.pixelSize: 12
                                }

                                Repeater {
                                    model: intgSection.items
                                    delegate: Rectangle {
                                        Layout.fillWidth: true
                                        radius: 8
                                        color: Theme.inputBg
                                        border.color: Theme.borderColor
                                        implicitHeight: rowCol.implicitHeight + 20

                                        ColumnLayout {
                                            id: rowCol
                                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                                            spacing: 4

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 8
                                                Text {
                                                    text: modelData.type === "mcp" ? "🧩" : "🔌"
                                                    font.pixelSize: 14
                                                }
                                                Text {
                                                    text: modelData.name
                                                    color: Theme.textPrimary
                                                    font.pixelSize: 13
                                                    font.bold: true
                                                }
                                                Rectangle {
                                                    radius: 4
                                                    color: Theme.surfaceBg
                                                    implicitWidth: tBadge.width + 12
                                                    implicitHeight: tBadge.height + 6
                                                    Text {
                                                        id: tBadge
                                                        anchors.centerIn: parent
                                                        text: modelData.type === "mcp" ? "MCP" : "API"
                                                        color: Theme.textSecondary
                                                        font.pixelSize: 10
                                                    }
                                                }
                                                Item { Layout.fillWidth: true }
                                                Switch {
                                                    checked: modelData.enabled
                                                    onToggled: App.setIntegrationEnabled(modelData.id, checked)
                                                }
                                                LcButton {
                                                    text: "Test"
                                                    secondary: true
                                                    implicitHeight: 30
                                                    onClicked: App.testIntegration(modelData.id)
                                                }
                                                LcButton {
                                                    text: "✕"
                                                    danger: true
                                                    implicitHeight: 30
                                                    onClicked: App.removeIntegration(modelData.id)
                                                }
                                            }
                                            Text {
                                                text: modelData.summary
                                                color: Theme.textMuted
                                                font.pixelSize: 11
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                            Text {
                                                visible: intgSection.testMsgId === modelData.id && intgSection.testMsg.length > 0
                                                text: intgSection.testMsg
                                                color: intgSection.testOk ? Theme.accent : Theme.btnDangerBg
                                                font.pixelSize: 11
                                                wrapMode: Text.WordWrap
                                                Layout.fillWidth: true
                                            }
                                        }
                                    }
                                }

                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                                // Add integration
                                Text {
                                    text: "Agregar integración"
                                    color: Theme.textPrimary
                                    font.pixelSize: 13
                                    font.bold: true
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text { text: "Tipo"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    ComboBox {
                                        id: typeCombo
                                        Layout.preferredWidth: 200
                                        model: [
                                            { label: "MCP Tool Server", value: "mcp" },
                                            { label: "API Service",     value: "api_service" },
                                        ]
                                        textRole: "label"
                                        valueRole: "value"
                                    }
                                }

                                // Campos MCP
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    visible: typeCombo.currentValue === "mcp"
                                    LcTextField { id: mcpName; Layout.fillWidth: true; placeholderText: "Nombre (ej. filesystem)" }
                                    LcTextField { id: mcpCmd;  Layout.fillWidth: true; placeholderText: "Comando stdio (ej. npx -y @modelcontextprotocol/server-filesystem .)" }
                                    LcButton {
                                        text: "Agregar MCP"
                                        enabled: mcpName.text.trim().length > 0 && mcpCmd.text.trim().length > 0
                                        onClicked: {
                                            if (App.saveMcpIntegration(mcpName.text.trim(), "local", mcpCmd.text.trim())) {
                                                mcpName.text = ""; mcpCmd.text = ""
                                            }
                                        }
                                    }
                                }

                                // Campos API Service
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    visible: typeCombo.currentValue === "api_service"
                                    LcTextField { id: apiName; Layout.fillWidth: true; placeholderText: "Nombre" }
                                    LcTextField { id: apiUrl;  Layout.fillWidth: true; placeholderText: "Base URL (https://…)" }
                                    LcTextField { id: apiKey;  Layout.fillWidth: true; placeholderText: "API key (opcional)"; echoMode: TextInput.Password }
                                    LcButton {
                                        text: "Agregar API Service"
                                        enabled: apiName.text.trim().length > 0 && apiUrl.text.trim().length > 0
                                        onClicked: {
                                            if (App.saveApiService("", apiName.text.trim(), apiUrl.text.trim(), apiKey.text, true)) {
                                                apiName.text = ""; apiUrl.text = ""; apiKey.text = ""
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── Agent Tools ──────────────────────────────────────────
                    ColumnLayout {
                        id: toolsSection
                        Layout.fillWidth: true
                        spacing: 10

                        property var items: []
                        property var groups: []
                        property int enabledTokens: 0
                        property int enabledCount: 0

                        function reload() {
                            var list = App.agentToolCatalog()
                            var order = [], byGroup = {}, tok = 0, cnt = 0
                            for (var i = 0; i < list.length; ++i) {
                                var t = list[i]
                                if (byGroup[t.group] === undefined) { byGroup[t.group] = []; order.push(t.group) }
                                byGroup[t.group].push(t)
                                if (t.enabled) { tok += t.approxTokens; cnt += 1 }
                            }
                            var g = []
                            for (var k = 0; k < order.length; ++k) {
                                var name = order[k], tools = byGroup[name], on = 0
                                for (var j = 0; j < tools.length; ++j) if (tools[j].enabled) on += 1
                                g.push({ name: name, tools: tools, on: on, total: tools.length })
                            }
                            items = list; groups = g; enabledTokens = tok; enabledCount = cnt
                        }
                        Component.onCompleted: reload()
                        Connections {
                            target: App
                            function onAgentToolsChanged() { toolsSection.reload() }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "AGENT TOOLS"
                                color: Theme.accent
                                font.pixelSize: 11
                                font.bold: true
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: toolsSection.enabledCount + " on · ~" + toolsSection.enabledTokens + " tok"
                                color: Theme.textMuted
                                font.pixelSize: 11
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: toolsInner.implicitHeight + 32

                            ColumnLayout {
                                id: toolsInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 14

                                Text {
                                    text: "Habilitá o deshabilitá las tools que se ofrecen al modelo. Apagar las que no usás ahorra contexto (clave en modelos locales chicos)."
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }

                                Repeater {
                                    model: toolsSection.groups
                                    delegate: ColumnLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 6

                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text {
                                                text: modelData.name
                                                color: Theme.textPrimary
                                                font.pixelSize: 13
                                                font.bold: true
                                            }
                                            Item { Layout.fillWidth: true }
                                            Text {
                                                text: modelData.on + "/" + modelData.total
                                                color: Theme.textMuted
                                                font.pixelSize: 11
                                            }
                                        }

                                        Repeater {
                                            model: modelData.tools
                                            delegate: Rectangle {
                                                required property var modelData
                                                Layout.fillWidth: true
                                                radius: 8
                                                color: Theme.inputBg
                                                border.color: Theme.borderColor
                                                implicitHeight: tRow.implicitHeight + 16

                                                RowLayout {
                                                    id: tRow
                                                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                                                    spacing: 8

                                                    ColumnLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 2
                                                        RowLayout {
                                                            spacing: 8
                                                            Text {
                                                                text: modelData.name
                                                                color: Theme.textPrimary
                                                                font.pixelSize: 13
                                                                font.bold: true
                                                                font.family: "monospace"
                                                            }
                                                            Text {
                                                                text: "~" + modelData.approxTokens
                                                                color: Theme.textMuted
                                                                font.pixelSize: 10
                                                            }
                                                        }
                                                        Text {
                                                            text: modelData.description
                                                            color: Theme.textMuted
                                                            font.pixelSize: 11
                                                            elide: Text.ElideRight
                                                            Layout.fillWidth: true
                                                        }
                                                    }
                                                    Switch {
                                                        checked: modelData.enabled
                                                        onToggled: App.setAgentToolEnabled(modelData.name, checked)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── Modelo maestro (ask_teacher) ─────────────────────────
                    ColumnLayout {
                        id: teacherSection
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: "MODELO MAESTRO (ask_teacher)"
                            color: Theme.accent
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: teacherInner.implicitHeight + 32

                            ColumnLayout {
                                id: teacherInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 12

                                Text {
                                    text: "Endpoint OpenAI-compatible de un modelo más capaz. La tool ask_teacher lo consulta para sub-problemas difíciles. Vacío = se usan las env vars LLAMACODE_TEACHER_*."
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }

                                Text { text: "Base URL"; color: Theme.textSecondary; font.pixelSize: 12 }
                                LcTextField {
                                    Layout.fillWidth: true
                                    text: App.agentTeacherUrl
                                    placeholderText: "https://api.openai.com  ·  http://localhost:8081"
                                    onEditingFinished: App.agentTeacherUrl = text
                                }

                                Text { text: "Modelo"; color: Theme.textSecondary; font.pixelSize: 12 }
                                LcTextField {
                                    Layout.fillWidth: true
                                    text: App.agentTeacherModel
                                    placeholderText: "gpt-4o  ·  qwen2.5-coder-32b  ·  default"
                                    onEditingFinished: App.agentTeacherModel = text
                                }

                                Text { text: "API key (opcional)"; color: Theme.textSecondary; font.pixelSize: 12 }
                                LcTextField {
                                    Layout.fillWidth: true
                                    text: App.agentTeacherKey
                                    placeholderText: "sk-…  (vacío para servers locales)"
                                    echoMode: TextInput.Password
                                    onEditingFinished: App.agentTeacherKey = text
                                }
                            }
                        }
                    }

                    // ── Data maintenance ───────────────────────────────────
                    ColumnLayout {
                        id: dataSection
                        Layout.fillWidth: true
                        spacing: 10

                        property var wipeItems: App.wipeCategories()

                        Text {
                            text: "DATA"
                            color: Theme.accent
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: dataInner.implicitHeight + 32

                            ColumnLayout {
                                id: dataInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 14

                                Text {
                                    text: "Data Backup"
                                    color: Theme.textPrimary
                                    font.pixelSize: 14
                                    font.bold: true
                                }

                                Text {
                                    text: "Exportá o importá tus chats, perfiles, settings, integraciones, skills y resultados guardados como JSON."
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    LcButton {
                                        text: "Export Data"
                                        onClicked: App.exportUserData()
                                    }
                                    LcButton {
                                        text: "Import Data"
                                        secondary: true
                                        onClicked: App.importUserData()
                                    }
                                    Item { Layout.fillWidth: true }
                                }

                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                                Text {
                                    text: "Danger Zone"
                                    color: Theme.btnDangerBg
                                    font.pixelSize: 14
                                    font.bold: true
                                }

                                Text {
                                    text: "Irreversible. Cada wipe apunta a una categoría; elegí exactamente qué querés borrar."
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }

                                Repeater {
                                    model: dataSection.wipeItems
                                    delegate: Rectangle {
                                        Layout.fillWidth: true
                                        radius: 8
                                        color: Theme.inputBg
                                        border.color: Theme.borderColor
                                        implicitHeight: wipeRow.implicitHeight + 18

                                        RowLayout {
                                            id: wipeRow
                                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                                            spacing: 12

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 3
                                                Text {
                                                    text: modelData.title
                                                    color: Theme.textPrimary
                                                    font.pixelSize: 13
                                                    font.bold: true
                                                }
                                                Text {
                                                    text: modelData.description
                                                    color: Theme.textMuted
                                                    font.pixelSize: 11
                                                    Layout.fillWidth: true
                                                    wrapMode: Text.WordWrap
                                                }
                                            }

                                            LcButton {
                                                text: "Wipe"
                                                danger: true
                                                implicitHeight: 30
                                                onClicked: {
                                                    wipeDialog.kind = modelData.kind
                                                    wipeDialog.label = modelData.title
                                                    wipeDialog.description = modelData.description
                                                    wipeConfirm.text = ""
                                                    wipeDialog.open()
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    LcDialog {
        id: wipeDialog
        title: "Confirm wipe"
        property string kind: ""
        property string label: ""
        property string description: ""
        standardButtons: Dialog.NoButton
        width: Math.min(520, root.width - 48)

        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: wipeDialog.label
                color: Theme.textPrimary
                font.pixelSize: 14
                font.bold: true
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Text {
                text: wipeDialog.description
                color: Theme.textSecondary
                font.pixelSize: 12
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Text {
                text: "Escribí WIPE para confirmar."
                color: Theme.textSecondary
                font.pixelSize: 12
            }
            LcTextField {
                id: wipeConfirm
                Layout.fillWidth: true
                placeholderText: "WIPE"
                onAccepted: {
                    if (text.trim() === "WIPE") {
                        App.wipeUserData(wipeDialog.kind, text.trim())
                        wipeDialog.close()
                    }
                }
            }
        }

        footer: Rectangle {
            color: Theme.popupHeaderBg
            height: 56
            radius: 12
            Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
            Row {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                spacing: 10
                LcButton {
                    text: (App.langV, App.l("common.cancel"))
                    secondary: true
                    onClicked: wipeDialog.close()
                }
                LcButton {
                    text: "Wipe"
                    danger: true
                    enabled: wipeConfirm.text.trim() === "WIPE"
                    onClicked: {
                        App.wipeUserData(wipeDialog.kind, wipeConfirm.text.trim())
                        wipeDialog.close()
                    }
                }
            }
        }
    }
}
