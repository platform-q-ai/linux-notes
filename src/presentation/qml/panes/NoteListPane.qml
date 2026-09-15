import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var noteListVm: null
    property var folderVm: null
    property var requestDeleteNote: null
    property var requestPurgeNote: null

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Label {
                text: {
                    if (root.noteListVm && root.noteListVm.showingTrash)
                        return qsTr("Trash")
                    if (root.noteListVm && root.noteListVm.searching)
                        return qsTr("Search results")
                    return qsTr("Notes")
                }
                font.bold: true
                Layout.fillWidth: true
            }
            ToolButton {
                text: qsTr("New")
                visible: !(root.noteListVm && root.noteListVm.showingTrash)
                onClicked: if (root.noteListVm) root.noteListVm.createNote()
            }
            ToolButton {
                text: qsTr("Move")
                visible: !(root.noteListVm && root.noteListVm.showingTrash)
                enabled: !!(root.noteListVm && root.noteListVm.selectedNoteId
                            && root.noteListVm.selectedNoteId.length > 0)
                onClicked: moveDialog.open()
            }
            ToolButton {
                text: (root.noteListVm && root.noteListVm.showingTrash)
                      ? qsTr("Restore") : qsTr("Trash")
                enabled: !!(root.noteListVm && root.noteListVm.selectedNoteId
                            && root.noteListVm.selectedNoteId.length > 0)
                onClicked: {
                    if (!root.noteListVm)
                        return
                    var id = root.noteListVm.selectedNoteId
                    if (root.noteListVm.showingTrash) {
                        root.noteListVm.restoreNote(id)
                    } else if (typeof root.requestDeleteNote === "function") {
                        root.requestDeleteNote(id)
                    } else {
                        root.noteListVm.deleteNote(id)
                    }
                }
            }
            ToolButton {
                text: qsTr("Delete forever")
                visible: !!(root.noteListVm && root.noteListVm.showingTrash)
                enabled: !!(root.noteListVm && root.noteListVm.selectedNoteId
                            && root.noteListVm.selectedNoteId.length > 0)
                onClicked: {
                    if (!root.noteListVm)
                        return
                    var id = root.noteListVm.selectedNoteId
                    if (typeof root.requestPurgeNote === "function")
                        root.requestPurgeNote(id)
                    else
                        root.noteListVm.purgeNote(id)
                }
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.noteListVm ? root.noteListVm.model : null
            currentIndex: -1
            delegate: ItemDelegate {
                width: ListView.view.width
                required property string noteId
                required property string title
                required property bool pinned
                required property int index
                text: (pinned ? "📌 " : "")
                      + (title && title.length ? title : qsTr("Untitled"))
                highlighted: ListView.isCurrentItem
                             || !!(root.noteListVm
                                   && root.noteListVm.selectedNoteId === noteId)
                onClicked: {
                    list.currentIndex = index
                    if (root.noteListVm)
                        root.noteListVm.selectedNoteId = noteId
                }
            }
        }

        Label {
            visible: !!(root.noteListVm && root.noteListVm.errorString
                        && root.noteListVm.errorString.length > 0)
            text: root.noteListVm ? root.noteListVm.errorString : ""
            color: "#b00020"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }

    Dialog {
        id: moveDialog
        title: qsTr("Move note to folder")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent

        ColumnLayout {
            anchors.fill: parent
            Label {
                text: qsTr("Choose destination folder")
            }
            ComboBox {
                id: folderCombo
                Layout.preferredWidth: 260
                textRole: "name"
                valueRole: "folderId"
                model: root.folderVm ? root.folderVm.model : null
            }
        }
        onOpened: {
            if (folderCombo.count > 0)
                folderCombo.currentIndex = 0
        }
        onAccepted: {
            if (!root.noteListVm || !root.noteListVm.selectedNoteId)
                return
            var target = folderCombo.currentValue
            if (!target || ("" + target).length === 0)
                return
            root.noteListVm.moveNote(root.noteListVm.selectedNoteId, "" + target)
        }
    }
}
