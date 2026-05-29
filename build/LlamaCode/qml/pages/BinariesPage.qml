import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LlamaCode 1.0

Item {
    id: root
    function openAddDialog() { addDlg.open() }

    // ---- Add dialog ----
    LcDialog {
        id: addDlg
        title: "Add Binary"
        width: 560
        height: 340

        onAccepted: {
            const newId = App.binaryRegistry.add(pathField.text, nameField.text,
                                                  flavorCombo.currentText, backendCombo.currentText, "")
            if (newId.length > 0) App.binaryRegistry.detectCapabilities(newId)
            pathField.text = ""; nameField.text = ""
        }

        contentItem: ColumnLayout {
            width: 520
            spacing: 12

            Text { text: "Path"; color: "#bac2de"; font.pixelSize: 12; font.bold: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                LcTextField { id: pathField; Layout.fillWidth: true; placeholderText: "Path to llama-server.exe" }
                LcButton { text: "Browse"; secondary: true; onClicked: fileDlg.open() }
            }

            Text { text: "Name"; color: "#bac2de"; font.pixelSize: 12; font.bold: true }
            LcTextField { id: nameField; Layout.fillWidth: true; placeholderText: "Optional display name" }

            RowLayout {
                Layout.fillWidth: true
                spacing: 14

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: "Flavor"; color: "#bac2de"; font.pixelSize: 12; font.bold: true }
                    ComboBox {
                        id: flavorCombo
                        Layout.fillWidth: true
                        model: ["official", "mtp-fork", "custom"]
                        background: Rectangle { color: "#11111b"; radius: 6; border.color: "#313244" }
                        contentItem: Text { text: flavorCombo.displayText; color: "#cdd6f4"; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: "Backend"; color: "#bac2de"; font.pixelSize: 12; font.bold: true }
                    ComboBox {
                        id: backendCombo
                        Layout.fillWidth: true
                        model: ["cpu", "cuda", "vulkan", "metal"]
                        background: Rectangle { color: "#11111b"; radius: 6; border.color: "#313244" }
                        contentItem: Text { text: backendCombo.displayText; color: "#cdd6f4"; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                    }
                }
            }
        }

        FileDialog {
            id: fileDlg
            title: "Select llama-server binary"
            nameFilters: ["Executables (*.exe *.bin *)", "All files (*)"]
            onAccepted: pathField.text = selectedFile.toString().replace(/^file:\/\/\//, "")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: "Binaries"
            subtitle: App.binaryRegistry.count + " registered"
            actionLabel: "+ Add Binary"
            onActionClicked: addDlg.open()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // List
            ListView {
                id: listView
                Layout.preferredWidth: 280
                Layout.fillHeight: true
                clip: true
                model: App.binaryRegistry
                currentIndex: -1

                ScrollBar.vertical: ScrollBar {}

                delegate: ItemDelegate {
                    width: listView.width
                    height: 64
                    highlighted: listView.currentIndex === index
                    background: Rectangle {
                        color: parent.highlighted ? "#313244" : (parent.hovered ? "#1e1e2e" : "transparent")
                    }
                    contentItem: Column {
                        anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                        spacing: 3
                        Row {
                            spacing: 6
                            Text {
                                text: pathValid ? "●" : "○"
                                font.pixelSize: 10
                                color: pathValid ? "#a6e3a1" : "#f38ba8"
                            }
                            Text {
                                text: name
                                font { pixelSize: 14; bold: true }
                                color: "#cdd6f4"
                            }
                        }
                        Text {
                            text: backend.toUpperCase() + " · " + flavor
                            font.pixelSize: 11
                            color: "#585b70"
                        }
                    }
                    onClicked: listView.currentIndex = index
                }

                Rectangle {
                    anchors.right: parent.right
                    width: 1; height: parent.height
                    color: "#313244"
                }
            }

            // Detail panel
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Empty state
                Text {
                    anchors.centerIn: parent
                    visible: listView.currentIndex < 0
                    text: "Select a binary"
                    color: "#585b70"
                    font.pixelSize: 14
                }

                // Detail
                BinaryDetail {
                    anchors.fill: parent
                    visible: listView.currentIndex >= 0
                    binId: listView.currentIndex >= 0
                           ? App.binaryRegistry.data(
                                 App.binaryRegistry.index(listView.currentIndex, 0),
                                 Qt.UserRole + 1)
                           : ""
                }
            }
        }
    }

    component BinaryDetail: Item {
        property string binId: ""
        property var binData: binId.length > 0 ? App.binaryRegistry.get(binId) : ({})

        onBinIdChanged: nameEditField.text = binData.name ?? ""
        onBinDataChanged: { if (!nameEditField.activeFocus) nameEditField.text = binData.name ?? "" }

        ColumnLayout {
            anchors { fill: parent; margins: 24 }
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                LcTextField {
                    id: nameEditField
                    Layout.fillWidth: true
                    text: binData.name ?? ""
                    font.pixelSize: 18
                    font.bold: true
                    Keys.onReturnPressed: applyRename()
                    Keys.onEnterPressed: applyRename()
                    function applyRename() {
                        const t = nameEditField.text.trim()
                        if (t.length > 0 && t !== binData.name)
                            App.binaryRegistry.update(binId, t,
                                binData.flavor ?? "official",
                                binData.backend ?? "cpu",
                                binData.versionHint ?? "",
                                binData.workingDirectory ?? "")
                    }
                }
                LcButton {
                    text: "Renombrar"
                    secondary: true
                    enabled: nameEditField.text.trim().length > 0 && nameEditField.text.trim() !== (binData.name ?? "")
                    onClicked: nameEditField.applyRename()
                }
            }

            GridLayout {
                columns: 2
                columnSpacing: 16
                rowSpacing: 8
                Layout.fillWidth: true

                Text { text: "Path";    color: "#585b70"; font.pixelSize: 12 }
                Text { text: binData.path ?? ""; color: "#a6adc8"; font.pixelSize: 12; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }

                Text { text: "Backend"; color: "#585b70"; font.pixelSize: 12 }
                Text { text: (binData.backend ?? "").toUpperCase(); color: "#89b4fa"; font.pixelSize: 12 }

                Text { text: "Flavor";  color: "#585b70"; font.pixelSize: 12 }
                Text { text: binData.flavor ?? ""; color: "#a6adc8"; font.pixelSize: 12 }

                Text { text: "Status";  color: "#585b70"; font.pixelSize: 12 }
                Text {
                    text: (binData.pathValid ?? false) ? "✓ Found" : "✗ Not found"
                    color: (binData.pathValid ?? false) ? "#a6e3a1" : "#f38ba8"
                    font.pixelSize: 12
                }

                Text { text: "Flags";   color: "#585b70"; font.pixelSize: 12 }
                Text {
                    text: (binData.hasCapabilities ?? false)
                          ? ((binData.supportedFlags?.length ?? 0) + " detected")
                          : "Not detected"
                    color: (binData.hasCapabilities ?? false) ? "#a6e3a1" : "#585b70"
                    font.pixelSize: 12
                }
            }

            Row {
                spacing: 8
                LcButton {
                    text: "Detect Capabilities"
                    enabled: binData.pathValid ?? false
                    onClicked: App.binaryRegistry.detectCapabilities(binId)
                }
                LcButton {
                    text: "Remove"
                    danger: true
                    onClicked: {
                        App.binaryRegistry.remove(binId)
                        listView.currentIndex = -1
                    }
                }
            }

            // Flags list
            Rectangle {
                visible: (binData.supportedFlags?.length ?? 0) > 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#11111b"
                radius: 6
                border.color: "#313244"
                clip: true

                ListView {
                    anchors { fill: parent; margins: 8 }
                    model: binData.supportedFlags ?? []
                    delegate: Text {
                        text: modelData
                        font { family: "Consolas,monospace"; pixelSize: 12 }
                        color: "#a6adc8"
                        height: 20
                    }
                    ScrollBar.vertical: ScrollBar {}
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
