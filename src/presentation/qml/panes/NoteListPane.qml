import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: root
    property var noteListVm
    property var folderVm

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Label { text: qsTr("Notes"); font.bold: true; Layout.fillWidth: true }
            Button {
                text: qsTr("+")
                enabled: folderVm && folderVm.selectedFolderId && folderVm.selectedFolderId.length > 0
                onClicked: if (noteListVm) noteListVm.createNote()
            }
            Button {
                text: qsTr("Del")
                enabled: noteListVm && noteListVm.selectedNoteId && noteListVm.selectedNoteId.length > 0
                onClicked: if (noteListVm) noteListVm.deleteNote(noteListVm.selectedNoteId)
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: noteListVm ? noteListVm.model : null
            delegate: ItemDelegate {
                width: list.width
                text: model.title
                highlighted: noteListVm && model.noteId === noteListVm.selectedNoteId
                onClicked: if (noteListVm) noteListVm.selectedNoteId = model.noteId
            }
        }
    }
}
