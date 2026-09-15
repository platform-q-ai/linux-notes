import QtQuick
import QtQuick.Controls
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
                placeholderText: qsTr("Select or create a note")
                property bool suppress: false

                function applyInline(kind) {
                    if (!root.editorVm)
                        return
                    if (selectionStart === selectionEnd)
                        return
                    root.editorVm.toggleInlineStyle(selectionStart, selectionEnd, kind)
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

    function pullFromVm() {
        if (!root.editorVm)
            return
        if (editor.text === root.editorVm.html)
            return
        editor.suppress = true
        editor.text = root.editorVm.html
        editor.suppress = false
        if (pinBox.checked !== root.editorVm.pinned)
            pinBox.checked = root.editorVm.pinned
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
    }
}
