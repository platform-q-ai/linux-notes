import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1100
    height: 720
    visible: true
    title: qsTr("Linux Notes")
    color: "#f5f5f7"

    // Context properties from composition_root: folderVm, noteListVm, editorVm
    property var folders: typeof folderVm !== "undefined" ? folderVm : null
    property var notes: typeof noteListVm !== "undefined" ? noteListVm : null
    property var editor: typeof editorVm !== "undefined" ? editorVm : null

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 12

            Label {
                text: qsTr("Linux Notes")
                font.bold: true
                font.pixelSize: 16
            }

            TextField {
                id: searchField
                Layout.preferredWidth: 280
                placeholderText: qsTr("Search notes…")
                onTextChanged: if (root.notes) root.notes.searchQuery = text
            }

            Item { Layout.fillWidth: true }

            Loader {
                source: "qrc:/notes/components/SaveStateBadge.qml"
                onLoaded: {
                    item.saveState = Qt.binding(function () {
                        return root.editor ? root.editor.saveState : "empty"
                    })
                    item.errorString = Qt.binding(function () {
                        return root.editor ? root.editor.errorString : ""
                    })
                }
            }

            ToolButton {
                text: qsTr("Save")
                enabled: root.editor && root.editor.dirty
                onClicked: if (root.editor) root.editor.saveNow()
            }
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        Loader {
            SplitView.preferredWidth: 220
            SplitView.minimumWidth: 160
            source: "qrc:/notes/panes/FolderPane.qml"
            onLoaded: {
                item.folderVm = Qt.binding(function () { return root.folders })
                item.folderSelected.connect(function (id) {
                    if (root.notes)
                        root.notes.folderId = id
                })
            }
        }

        Loader {
            SplitView.preferredWidth: 280
            SplitView.minimumWidth: 180
            source: "qrc:/notes/panes/NoteListPane.qml"
            onLoaded: {
                item.noteListVm = Qt.binding(function () { return root.notes })
                item.noteSelected.connect(function (id) {
                    if (root.editor)
                        root.editor.openNote(id)
                })
            }
        }

        Loader {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 320
            source: "qrc:/notes/panes/EditorPane.qml"
            onLoaded: {
                item.editorVm = Qt.binding(function () { return root.editor })
            }
        }
    }

    Component.onCompleted: {
        if (root.folders)
            root.folders.refresh()
        if (root.notes)
            root.notes.refresh()
    }

    Connections {
        target: root.folders
        function onSelectedFolderIdChanged() {
            if (root.notes && root.folders)
                root.notes.folderId = root.folders.selectedFolderId
        }
    }

    Connections {
        target: root.notes
        function onOpenNoteRequested(noteId) {
            if (root.editor)
                root.editor.openNote(noteId)
        }
        function onNoteCreated(noteId) {
            if (root.editor)
                root.editor.openNote(noteId)
        }
        function onNoteDeleted(noteId) {
            if (root.editor && root.editor.noteId === noteId)
                root.editor.closeNote()
        }
    }

    Connections {
        target: root.editor
        function onSaved(noteId, title, revision, pinned) {
            if (root.notes)
                root.notes.applySummaryTitle(noteId, title, revision, pinned)
        }
    }
}
