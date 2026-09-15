import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root
    property var editorVm: null

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            spacing: 4
            enabled: root.editorVm && root.editorVm.noteId
                     && root.editorVm.noteId.length > 0
                     && !(root.editorVm.noteTrashed)

            ToolButton {
                text: "B"
                font.bold: true
                onClicked: editor.applyInline("bold")
            }
            ToolButton {
                text: "I"
                font.italic: true
                onClicked: editor.applyInline("italic")
            }
            ToolButton {
                text: "U"
                font.underline: true
                onClicked: editor.applyInline("underline")
            }
            ToolSeparator {}
            ToolButton {
                text: qsTr("Checklist")
                ToolTip.text: qsTr("Insert checklist item")
                ToolTip.visible: hovered
                onClicked: if (root.editorVm) root.editorVm.insertChecklist()
            }
            ToolButton {
                text: qsTr("Attach…")
                ToolTip.text: qsTr("Attach a local file (not remote)")
                ToolTip.visible: hovered
                onClicked: attachDialog.open()
            }
            ToolSeparator {}
            ToolButton {
                text: qsTr("Undo")
                enabled: editor.canUndo
                onClicked: editor.undo()
            }
            ToolButton {
                text: qsTr("Redo")
                enabled: editor.canRedo
                onClicked: editor.redo()
            }
            Item { Layout.fillWidth: true }
            CheckBox {
                id: pinBox
                text: qsTr("Pinned")
                checked: root.editorVm ? root.editorVm.pinned : false
                onClicked: if (root.editorVm) root.editorVm.pinned = checked
            }
        }

        // Attachment strip (local refs only; no remote/active content).
        Frame {
            id: attachStrip
            visible: attachRepeater.count > 0
            Layout.fillWidth: true
            padding: 4
            background: Rectangle {
                color: "#f3f5f8"
                radius: 4
                border.color: "#d0d7de"
            }
            RowLayout {
                anchors.fill: parent
                spacing: 6
                Label {
                    text: qsTr("Attachments:")
                    font.bold: true
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        id: attachRepeater
                        model: root.attachmentModel()
                        delegate: RowLayout {
                            spacing: 2
                            Label {
                                text: modelData.name
                                elide: Text.ElideMiddle
                                Layout.maximumWidth: 160
                            }
                            ToolButton {
                                text: "×"
                                flat: true
                                onClicked: {
                                    if (root.editorVm)
                                        root.editorVm.removeAttachment(modelData.id)
                                }
                            }
                        }
                    }
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            TextArea {
                id: editor
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.RichText
                persistentSelection: true
                selectByMouse: true
                readOnly: !(root.editorVm && root.editorVm.noteId
                            && root.editorVm.noteId.length > 0)
                          || !!(root.editorVm && root.editorVm.noteTrashed)
                placeholderText: qsTr("Select or create a note")
                property bool suppress: false

                function applyInline(kind) {
                    if (!root.editorVm)
                        return
                    if (root.editorVm.noteTrashed)
                        return
                    if (selectionStart === selectionEnd)
                        return
                    root.editorVm.toggleInlineStyle(selectionStart, selectionEnd, kind)
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    // Let TextArea handle selection; intercept double-click on markers.
                    propagateComposedEvents: true
                    onDoubleClicked: function (mouse) {
                        if (!root.editorVm)
                            return
                        if (root.editorVm.noteTrashed)
                            return
                        // QTextDocument position from positionAt — not plain_text.
                        editor.forceActiveFocus()
                        const pos = editor.positionAt(mouse.x, mouse.y)
                        editor.cursorPosition = pos
                        if (root.editorVm.toggleChecklistAtDocumentPosition) {
                            if (root.editorVm.toggleChecklistAtDocumentPosition(pos)) {
                                mouse.accepted = true
                                return
                            }
                        } else if (root.editorVm.toggleChecklistAtPlainOffset) {
                            // Legacy fallback only; plain offsets ≠ document coords.
                            if (root.editorVm.toggleChecklistAtPlainOffset(pos)) {
                                mouse.accepted = true
                                return
                            }
                        }
                        mouse.accepted = false
                    }
                    onPressed: function (mouse) { mouse.accepted = false }
                }

                Keys.onPressed: function (event) {
                    if (!(event.modifiers & Qt.ControlModifier))
                        return
                    if (event.key === Qt.Key_B) {
                        applyInline("bold"); event.accepted = true
                    } else if (event.key === Qt.Key_I) {
                        applyInline("italic"); event.accepted = true
                    } else if (event.key === Qt.Key_U) {
                        applyInline("underline"); event.accepted = true
                    }
                }

                onTextChanged: {
                    if (suppress)
                        return
                    if (!root.editorVm)
                        return
                    if (root.editorVm.html === text)
                        return
                    root.editorVm.html = text
                    root.editorVm.markUndoRedo(canUndo, canRedo)
                    root.refreshAttachments()
                }
                onCanUndoChanged: if (root.editorVm)
                    root.editorVm.markUndoRedo(canUndo, canRedo)
                onCanRedoChanged: if (root.editorVm)
                    root.editorVm.markUndoRedo(canUndo, canRedo)
            }
        }

        Label {
            visible: root.editorVm && root.editorVm.errorString
                     && root.editorVm.errorString.length > 0
            text: root.editorVm ? root.editorVm.errorString : ""
            color: "#b00020"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }

    FileDialog {
        id: attachDialog
        title: qsTr("Attach local file")
        fileMode: FileDialog.OpenFile
        nameFilters: [
            qsTr("Images (*.png *.jpg *.jpeg *.gif *.webp)"),
            qsTr("Documents (*.pdf *.txt *.md)"),
            qsTr("All files (*)")
        ]
        onAccepted: {
            if (!root.editorVm)
                return
            var path = ""
            if (selectedFile)
                path = selectedFile
            root.editorVm.attachLocalFile(path)
        }
    }

    property var _attachModel: []

    function attachmentModel() {
        return root._attachModel
    }

    function refreshAttachments() {
        if (!root.editorVm || !root.editorVm.attachmentIds) {
            root._attachModel = []
            return
        }
        var ids = root.editorVm.attachmentIds()
        var names = root.editorVm.attachmentNames()
        var rows = []
        for (var i = 0; i < ids.length; ++i) {
            rows.push({ id: ids[i], name: (names[i] || ids[i]) })
        }
        root._attachModel = rows
    }

    function pullFromVm() {
        if (!root.editorVm)
            return
        if (editor.text !== root.editorVm.html) {
            editor.suppress = true
            editor.text = root.editorVm.html
            editor.suppress = false
        }
        if (pinBox.checked !== root.editorVm.pinned)
            pinBox.checked = root.editorVm.pinned
        root.refreshAttachments()
    }

    Connections {
        target: root.editorVm
        ignoreUnknownSignals: true
        function onHtmlChanged() { root.pullFromVm() }
        function onNoteIdChanged() { root.pullFromVm() }
        function onPinnedChanged() {
            if (root.editorVm && pinBox.checked !== root.editorVm.pinned)
                pinBox.checked = root.editorVm.pinned
        }
        function onAttachmentChanged() { root.pullFromVm() }
    }
}
