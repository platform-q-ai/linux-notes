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

    property var folders: typeof folderVm !== "undefined" ? folderVm : null
    property var notes: typeof noteListVm !== "undefined" ? noteListVm : null
    property var editor: typeof editorVm !== "undefined" ? editorVm : null
    property string keepBothMessage: ""

    header: ToolBar {
        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 4

            RowLayout {
                spacing: 12
                Layout.fillWidth: true

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
                        if (!item) return
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
                    enabled: !!(root.editor && root.editor.dirty)
                    onClicked: if (root.editor) root.editor.saveNow()
                }
            }

            Label {
                id: keepBothBanner
                visible: root.keepBothMessage.length > 0
                text: root.keepBothMessage
                color: "#8a6d00"
                background: Rectangle {
                    color: "#fff3cd"
                    radius: 3
                }
                leftPadding: 8
                rightPadding: 8
                topPadding: 4
                bottomPadding: 4
                Layout.fillWidth: true
                wrapMode: Text.Wrap
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
                if (!item) return
                item.folderVm = Qt.binding(function () { return root.folders })
            }
        }

        Loader {
            SplitView.preferredWidth: 280
            SplitView.minimumWidth: 180
            source: "qrc:/notes/panes/NoteListPane.qml"
            onLoaded: {
                if (!item) return
                item.noteListVm = Qt.binding(function () { return root.notes })
                item.requestDeleteNote = root.deleteNoteSafely
            }
        }

        Loader {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 320
            source: "qrc:/notes/panes/EditorPane.qml"
            onLoaded: {
                if (!item) return
                item.editorVm = Qt.binding(function () { return root.editor })
            }
        }
    }

    function deleteNoteSafely(noteId) {
        if (!root.notes || !noteId || noteId.length === 0)
            return
        if (root.editor && root.editor.noteId === noteId
                && (root.editor.dirty || root.editor.saving)) {
            var ok = root.editor.flushPendingSavesBlocking()
            if (!ok)
                return
        }
        root.notes.deleteNote(noteId)
    }

    Component.onCompleted: {
        if (root.folders)
            root.folders.refresh()
        if (root.notes)
            root.notes.refresh()
    }

    Connections {
        target: root.folders
        enabled: root.folders !== null
        ignoreUnknownSignals: true
        function onSelectedFolderIdChanged() {
            if (root.notes && root.folders)
                root.notes.folderId = root.folders.selectedFolderId
        }
    }

    Connections {
        target: root.notes
        enabled: root.notes !== null
        ignoreUnknownSignals: true
        function onOpenNoteRequested(noteId) {
            if (root.editor)
                root.editor.openNote(noteId)
        }
        function onNoteCreated(noteId) {
            void noteId
        }
        function onNoteDeleted(noteId) {
            if (root.editor && root.editor.noteId === noteId)
                root.editor.closeNote()
        }
    }

    Connections {
        target: root.editor
        enabled: root.editor !== null
        ignoreUnknownSignals: true
        function onSaved(noteId, title, revision, pinned) {
            if (root.notes)
                root.notes.applySummaryTitle(noteId, title, revision, pinned)
        }
        function onKeepBothNotice(message) {
            root.keepBothMessage = message
            if (root.notes)
                root.notes.refresh()
            keepBothClear.restart()
        }
    }

    Timer {
        id: keepBothClear
        interval: 8000
        repeat: false
        onTriggered: root.keepBothMessage = ""
    }
}
