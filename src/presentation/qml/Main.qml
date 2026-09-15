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
    // Context property is exitGate; local alias avoids shadowing.
    property var gate: typeof exitGate !== "undefined" ? exitGate : null
    property string keepBothMessage: ""
    property string closeFailMessage: ""
    property bool allowClose: false

    // Veto normal window/app close when pending flush fails. aboutToQuit is too late.
    onClosing: function (close) {
        if (root.allowClose)
            return
        var g = root.gate
        if (!g) {
            // Fallback: flush editor directly if gate not wired.
            if (root.editor && (root.editor.dirty || root.editor.saving)) {
                var ok = root.editor.flushPendingSavesBlocking()
                if (!ok) {
                    close.accepted = false
                    root.closeFailMessage = root.editor.errorString.length > 0
                            ? qsTr("Could not save your note: %1").arg(root.editor.errorString)
                            : qsTr("Could not save your note. The window will stay open so you can retry.")
                    return
                }
            }
            root.allowClose = true
            return
        }
        if (g.quitAuthorized) {
            root.allowClose = true
            return
        }
        var flushed = g.requestClose()
        if (!flushed) {
            close.accepted = false
            root.closeFailMessage = g.blockReason
            return
        }
        root.allowClose = true
        root.closeFailMessage = ""
        // requestClose authorized quit; accept this close event.
    }

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

            Rectangle {
                id: closeFailBanner
                visible: root.closeFailMessage.length > 0
                color: "#f8d7da"
                radius: 3
                Layout.fillWidth: true
                implicitHeight: closeFailRow.implicitHeight + 8

                RowLayout {
                    id: closeFailRow
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        text: root.closeFailMessage
                        color: "#721c24"
                        wrapMode: Text.Wrap
                    }
                    Button {
                        text: qsTr("Retry save & quit")
                        onClicked: {
                            var g = root.gate
                            var ok = false
                            if (g)
                                ok = g.retryClose()
                            else if (root.editor)
                                ok = root.editor.flushPendingSavesBlocking()
                            if (ok) {
                                root.closeFailMessage = ""
                                root.allowClose = true
                                root.close()
                            } else if (g) {
                                root.closeFailMessage = g.blockReason
                            } else if (root.editor) {
                                root.closeFailMessage = root.editor.errorString
                            }
                        }
                    }
                    Button {
                        text: qsTr("Keep editing")
                        onClicked: {
                            if (root.gate)
                                root.gate.acknowledgeBlock()
                            root.closeFailMessage = ""
                        }
                    }
                }
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
                item.noteListVm = Qt.binding(function () { return root.notes })
            }
        }

        Loader {
            SplitView.preferredWidth: 280
            SplitView.minimumWidth: 180
            source: "qrc:/notes/panes/NoteListPane.qml"
            onLoaded: {
                if (!item) return
                item.noteListVm = Qt.binding(function () { return root.notes })
                item.folderVm = Qt.binding(function () { return root.folders })
                item.requestDeleteNote = root.deleteNoteSafely
                item.requestPurgeNote = root.purgeNoteConfirmed
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
        // Soft-delete into trash (default).
        root.notes.deleteNote(noteId)
    }

    property string pendingPurgeNoteId: ""

    function purgeNoteConfirmed(noteId) {
        if (!root.notes || !noteId || noteId.length === 0)
            return
        root.pendingPurgeNoteId = noteId
        purgeConfirmDialog.open()
    }

    Dialog {
        id: purgeConfirmDialog
        title: qsTr("Delete forever?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            text: qsTr("Permanently delete this note and its attachments? This cannot be undone.")
            wrapMode: Text.Wrap
            width: 320
        }
        onAccepted: {
            if (root.notes && root.pendingPurgeNoteId.length > 0) {
                if (root.editor && root.editor.noteId === root.pendingPurgeNoteId)
                    root.editor.closeNote()
                root.notes.purgeNote(root.pendingPurgeNoteId)
            }
            root.pendingPurgeNoteId = ""
        }
        onRejected: root.pendingPurgeNoteId = ""
    }

    Component.onCompleted: {
        if (typeof exitGate !== "undefined" && exitGate)
            root.gate = exitGate
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
        function onSelectedNoteIdChanged() {
            // Coherence: empty selection after trash/move/folder/search must
            // not leave a stale editor open on a missing note.
            if (!root.notes || !root.editor)
                return
            if (!root.notes.selectedNoteId
                    || root.notes.selectedNoteId.length === 0) {
                if (root.editor.noteId && root.editor.noteId.length > 0)
                    root.editor.closeNote()
            }
        }
        function onNoteCreated(noteId) {
            void noteId
        }
        function onNoteDeleted(noteId) {
            if (root.editor && root.editor.noteId === noteId)
                root.editor.closeNote()
        }
        function onNotePurged(noteId) {
            if (root.editor && root.editor.noteId === noteId)
                root.editor.closeNote()
        }
        function onNoteRestored(noteId) {
            void noteId
            if (root.notes)
                root.notes.refresh()
        }
        function onNoteMoved(noteId, folderId) {
            void noteId
            void folderId
            if (root.notes)
                root.notes.refresh()
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

    Connections {
        target: root.gate
        enabled: root.gate !== null
        ignoreUnknownSignals: true
        function onCloseFailed(reason) {
            root.closeFailMessage = reason
        }
        function onCloseSucceeded() {
            root.closeFailMessage = ""
        }
    }

    Timer {
        id: keepBothClear
        interval: 8000
        repeat: false
        onTriggered: root.keepBothMessage = ""
    }
}
