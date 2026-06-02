import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LlamaCode 1.0

Item {
    id: root

    function openAddDialog() { addDlg.open() }

    LcDialog {
        id: addDlg
        title: (App.langV, App.l("models.addRoot"))
        width: 560
        height: 300

        onAccepted: {
            App.rootRegistry.add(pathField.text, labelField.text, scanCombo.currentText, [])
            pathField.text = ""; labelField.text = ""
        }

        contentItem: ColumnLayout {
            width: 520
            spacing: 12

            Text { text: (App.langV, App.l("models.path")); color: Theme.dialogLabel; font.pixelSize: 12; font.bold: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                LcTextField { id: pathField; Layout.fillWidth: true; placeholderText: "Folder with GGUF files" }
                LcButton { text: (App.langV, App.l("common.browse")); secondary: true; onClicked: folderDlg.open() }
            }

            Text { text: (App.langV, App.l("models.label")); color: Theme.dialogLabel; font.pixelSize: 12; font.bold: true }
            LcTextField { id: labelField; Layout.fillWidth: true; placeholderText: "Optional label" }

            Text { text: (App.langV, App.l("models.scanMode")); color: Theme.dialogLabel; font.pixelSize: 12; font.bold: true }
            ComboBox {
                id: scanCombo
                Layout.fillWidth: true
                model: ["manual", "startup", "watch"]
                background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                contentItem: Text { text: scanCombo.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
            }
        }

        FolderDialog {
            id: folderDlg
            title: "Select model folder"
            onAccepted: pathField.text = selectedFolder.toString().replace("file:///", "")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: (App.langV, App.l("models.title"))
            subtitle: {
                const _lang = App.langV
                return App.rootRegistry.count + " " + App.l("models.roots") + " · " + App.modelCatalog.count + " " + App.l("models.modelsCount")
            }
            actionLabel: {
                const _lang = App.langV
                return App.rootRegistry.scanning ? App.l("models.scanning") : App.l("models.addAction")
            }
            onActionClicked: if (!App.rootRegistry.scanning) addDlg.open()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ListView {
                id: rootList
                Layout.preferredWidth: 260
                Layout.fillHeight: true
                clip: true
                model: App.rootRegistry
                ScrollBar.vertical: LcScrollBar {}

                delegate: ItemDelegate {
                    width: rootList.width
                    height: 60
                    highlighted: rootList.currentIndex === index
                    background: Rectangle {
                        color: parent.highlighted ? Theme.highlight : (parent.hovered ? Theme.hoverBg : "transparent")
                    }
                    contentItem: Column {
                        anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                        spacing: 3
                        Row {
                            spacing: 6
                            Text { text: isOnline ? "●" : "○"; font.pixelSize: 10; color: isOnline ? Theme.successText : Theme.errorText }
                            Text { text: label; font.pixelSize: 14; font.bold: true; color: Theme.textPrimary }
                        }
                        Text { width: rootList.width - 32; text: path; font.pixelSize: 11; color: Theme.textMuted; elide: Text.ElideMiddle }
                        Text { text: scanMode; font.pixelSize: 10; color: Theme.textMuted }
                    }
                    onClicked: { rootList.currentIndex = index; App.modelCatalog.filterRootId = rootId }
                }

                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.borderColor }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    height: 44
                    color: Theme.surfaceBg
                    visible: rootList.currentIndex >= 0

                    Row {
                        anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                        spacing: 8

                        LcButton {
                            text: (App.langV, App.l("models.scan"))
                            onClicked: {
                                const id = App.rootRegistry.data(App.rootRegistry.index(rootList.currentIndex, 0), 257)
                                App.rootRegistry.scan(id)
                            }
                        }
                        LcButton { text: (App.langV, App.l("models.scanAll")); secondary: true; onClicked: App.rootRegistry.scanAll() }
                        LcButton {
                            text: (App.langV, App.l("models.removeRoot"))
                            danger: true
                            onClicked: {
                                const id = App.rootRegistry.data(App.rootRegistry.index(rootList.currentIndex, 0), 257)
                                App.rootRegistry.remove(id)
                                rootList.currentIndex = -1
                                App.modelCatalog.filterRootId = ""
                            }
                        }
                    }

                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderColor }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    color: Theme.baseBg
                    Row {
                        anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                        spacing: 8
                        LcTextField {
                            width: 180; height: 28
                            placeholderText: (App.langV, App.l("models.filterFamily"))
                            onTextChanged: App.modelCatalog.filterFamily = text
                        }
                        CheckBox {
                            text: (App.langV, App.l("models.visionOnly"))
                            contentItem: Text { text: parent.text; color: Theme.textSecondary; font.pixelSize: 12; leftPadding: parent.indicator.width + 6 }
                            onCheckedChanged: App.modelCatalog.filterVisionOnly = checked
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderColor }
                }

                ListView {
                    id: modelList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: App.modelCatalog
                    ScrollBar.vertical: LcScrollBar {}

                    delegate: Rectangle {
                        width: modelList.width
                        height: 56
                        color: modelList.currentIndex === index ? Theme.highlight : (hovered ? Theme.hoverBg : "transparent")
                        property bool hovered: false
                        HoverHandler { onHoveredChanged: parent.hovered = hovered }
                        MouseArea { anchors.fill: parent; onClicked: modelList.currentIndex = index }

                        Row {
                            anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                            spacing: 10
                            Column {
                                spacing: 3
                                Row {
                                    spacing: 6
                                    Text { text: isVision ? "👁" : (isDraft ? "⚡" : "📄"); font.pixelSize: 14 }
                                    Text { text: fileName; font.pixelSize: 13; font.bold: true; color: isAvailable ? Theme.textPrimary : Theme.textMuted }
                                }
                                Row {
                                    spacing: 8
                                    Text { text: family;    font.pixelSize: 11; color: Theme.accent }
                                    Text { text: quant;     font.pixelSize: 11; color: Theme.successText }
                                    Text { text: sizeLabel; font.pixelSize: 11; color: Theme.textMuted }
                                }
                            }
                        }

                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.surfaceBg }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: parent.count === 0
                        text: {
                            const _lang = App.langV
                            return rootList.currentIndex < 0 ? App.l("models.selectRoot") : App.l("models.noModels")
                        }
                        color: Theme.textMuted
                        font.pixelSize: 14
                    }
                }
            }
        }
    }
}
