import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var folderVm: null
    property var noteListVm: null
    // Optional external sink if Main does not bind Connections.
    property var onSelect: null

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Label {
                text: qsTr("Folders")
                font.bold: true
                Layout.fillWidth: true
            }
            ToolButton {
                text: "+"
                onClicked: newFolderDialog.open()
            }
            ToolButton {
                text: qsTr("Ren")
                enabled: !!(root.folderVm && root.folderVm.selectedFolderId
                            && root.folderVm.selectedFolderId.length > 0
                            && root.folderVm.selectedFolderId !== "root")
                onClicked: {
                    if (!root.folderVm)
                        return
                    renameField.text = ""
                    renameFolderDialog.open()
                }
            }
            ToolButton {
                text: qsTr("Del")
                enabled: !!(root.folderVm && root.folderVm.selectedFolderId
                            && root.folderVm.selectedFolderId.length > 0
                            && root.folderVm.selectedFolderId !== "root")
                onClicked: {
                    if (root.folderVm)
                        root.folderVm.deleteFolder(root.folderVm.selectedFolderId)
                }
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.folderVm ? root.folderVm.model : null
            delegate: ItemDelegate {
                width: ListView.view.width
                required property string folderId
                required property string name
                required property string parentId
                text: (parentId && parentId.length ? "  " : "") + name
                highlighted: !!(root.folderVm
                                && root.folderVm.selectedFolderId === folderId
                                && !(root.noteListVm && root.noteListVm.showingTrash))
                onClicked: {
                    if (root.noteListVm && root.noteListVm.showingTrash)
                        root.noteListVm.hideTrash()
                    if (root.folderVm)
                        root.folderVm.selectedFolderId = folderId
                }
            }
        }

        ItemDelegate {
            Layout.fillWidth: true
            text: qsTr("🗑 Trash")
            highlighted: !!(root.noteListVm && root.noteListVm.showingTrash)
            onClicked: {
                if (root.noteListVm)
                    root.noteListVm.showTrash()
            }
        }

        Label {
            visible: !!(root.folderVm && root.folderVm.errorString
                        && root.folderVm.errorString.length > 0)
            text: root.folderVm ? root.folderVm.errorString : ""
            color: "#b00020"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }

    Dialog {
        id: newFolderDialog
        title: qsTr("New folder")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        TextField {
            id: nameField
            placeholderText: qsTr("Folder name")
            width: parent ? parent.width : 200
        }
        onAccepted: {
            if (root.folderVm && nameField.text.trim().length > 0)
                root.folderVm.createFolder(nameField.text.trim(),
                                           root.folderVm.selectedFolderId)
            nameField.text = ""
        }
    }

    Dialog {
        id: renameFolderDialog
        title: qsTr("Rename folder")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        TextField {
            id: renameField
            placeholderText: qsTr("New name")
            width: parent ? parent.width : 200
        }
        onAccepted: {
            if (root.folderVm && renameField.text.trim().length > 0
                    && root.folderVm.selectedFolderId
                    && root.folderVm.selectedFolderId !== "root") {
                root.folderVm.renameFolder(root.folderVm.selectedFolderId,
                                           renameField.text.trim())
            }
            renameField.text = ""
        }
    }
}
