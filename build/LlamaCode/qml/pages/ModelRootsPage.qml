import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LlamaCode 1.0

Item {
    id: root

    LcDialog {
        id: addDlg
        title: "Add Model Root"
        width: 560
        height: 300

        onAccepted: {
            App.rootRegistry.add(pathField.text, labelField.text,
                                  scanCombo.currentText, [])
            pathField.text = ""; labelField.text = ""
        }

        contentItem: ColumnLayout {
            width: 520
            spacing: 12

            Text { text: "Path"; color: "#bac2de"; font.pixelSize: 12; font.bold: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                LcTextField { id: pathField; Layout.fillWidth: true; placeholderText: "Folder with GGUF files" }
                LcButton {
                    text: "Browse"
                    secondary: true
                    onClicked: folderDlg.open()
                }
            }

            Text { text: "Label"; color: "#bac2de"; font.pixelSize: 12; font.bold: true }
            LcTextField { id: labelField; Layout.fillWidth: true; placeholderText: "Optional label" }

            Text { text: "Scan mode"; color: "#bac2de"; font.pixelSize: 12; font.bold: true }
            ComboBox {
                id: scanCombo
                Layout.fillWidth: true
                model: ["manual", "startup", "watch"]
                background: Rectangle { color: "#11111b"; radius: 6; border.color: "#313244" }
                contentItem: Text { text: scanCombo.displayText; color: "#cdd6f4"; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
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
            title: "Model Roots"
            subtitle: App.rootRegistry.count + " roots · " + App.modelCatalog.count + " models"
            actionLabel: App.rootRegistry.scanning ? "Scanning…" : "+ Add Root"
            onActionClicked: if (!App.rootRegistry.scanning) addDlg.open()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Root list
            ListView {
                id: rootList
                Layout.preferredWidth: 260
                Layout.fillHeight: true
                clip: true
                model: App.rootRegistry
                ScrollBar.vertical: ScrollBar {}

                delegate: ItemDelegate {
                    width: rootList.width
                    height: 60
                    highlighted: rootList.currentIndex === index
                    background: Rectangle {
                        color: parent.highlighted ? "#313244" : (parent.hovered ? "#1e1e2e" : "transparent")
                    }
                    contentItem: Column {
                        anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                        spacing: 3
                        Row {
                            spacing: 6
                            Text {
                                text: isOnline ? "●" : "○"
                                font.pixelSize: 10
                                color: isOnline ? "#a6e3a1" : "#f38ba8"
                            }
                            Text { text: label; font.pixelSize: 14; font.bold: true; color: "#cdd6f4" }
                        }
                        Text {
                            width: rootList.width - 32
                            text: path
                            font.pixelSize: 11
                            color: "#7f849c"
                            elide: Text.ElideMiddle
                        }
                        Text { text: scanMode; font.pixelSize: 10; color: "#585b70" }
                    }
                    onClicked: {
                        rootList.currentIndex = index
                        App.modelCatalog.filterRootId = rootId
                    }
                }

                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#313244" }
            }

            // Model catalog panel
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Root actions bar
                Rectangle {
                    Layout.fillWidth: true
                    height: 44
                    color: "#181825"
                    visible: rootList.currentIndex >= 0

                    Row {
                        anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                        spacing: 8

                        LcButton {
                            text: "Scan"
                            onClicked: {
                                const id = App.rootRegistry.data(
                                               App.rootRegistry.index(rootList.currentIndex, 0), 257)
                                App.rootRegistry.scan(id)
                            }
                        }
                        LcButton {
                            text: "Scan All"
                            secondary: true
                            onClicked: App.rootRegistry.scanAll()
                        }
                        LcButton {
                            text: "Remove Root"
                            danger: true
                            onClicked: {
                                const id = App.rootRegistry.data(
                                               App.rootRegistry.index(rootList.currentIndex, 0), 257)
                                App.rootRegistry.remove(id)
                                rootList.currentIndex = -1
                                App.modelCatalog.filterRootId = ""
                            }
                        }
                    }

                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#313244" }
                }

                // Filter row
                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    color: "#1e1e2e"
                    Row {
                        anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                        spacing: 8
                        LcTextField {
                            width: 180
                            height: 28
                            placeholderText: "Filter by family…"
                            onTextChanged: App.modelCatalog.filterFamily = text
                        }
                        CheckBox {
                            text: "Vision only"
                            contentItem: Text { text: parent.text; color: "#a6adc8"; font.pixelSize: 12; leftPadding: parent.indicator.width + 6 }
                            onCheckedChanged: App.modelCatalog.filterVisionOnly = checked
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#313244" }
                }

                // Model list
                ListView {
                    id: modelList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: App.modelCatalog
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Rectangle {
                        width: modelList.width
                        height: 56
                        color: modelList.currentIndex === index ? "#313244" : (hovered ? "#1e1e2e" : "transparent")
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
                                    Text {
                                        text: isVision ? "👁" : (isDraft ? "⚡" : "📄")
                                        font.pixelSize: 14
                                    }
                                    Text {
                                        text: fileName
                                        font { pixelSize: 13; bold: true }
                                        color: isAvailable ? "#cdd6f4" : "#585b70"
                                    }
                                }
                                Row {
                                    spacing: 8
                                    Text { text: family;     font.pixelSize: 11; color: "#89b4fa" }
                                    Text { text: quant;      font.pixelSize: 11; color: "#a6e3a1" }
                                    Text { text: sizeLabel;  font.pixelSize: 11; color: "#585b70" }
                                }
                            }
                        }

                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#181825" }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: parent.count === 0
                        text: rootList.currentIndex < 0 ? "Select a root to view models"
                                                        : "No models found. Click Scan."
                        color: "#585b70"
                        font.pixelSize: 14
                    }
                }
            }
        }
    }
}
