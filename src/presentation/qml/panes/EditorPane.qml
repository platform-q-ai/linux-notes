import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: root
    property var editorVm

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Label {
                text: editorVm && editorVm.noteId.length > 0 ? qsTr("Editor") : qsTr("No note selected")
                font.bold: true
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Bold")
                enabled: editorVm && editorVm.noteId.length > 0
                onClicked: wrapSelection("b")
            }
            Button {
                text: qsTr("Italic")
                enabled: editorVm && editorVm.noteId.length > 0
                onClicked: wrapSelection("i")
            }
            Button {
                text: qsTr("Underline")
                enabled: editorVm && editorVm.noteId.length > 0
                onClicked: wrapSelection("u")
            }
            Button {
                text: qsTr("Undo")
                enabled: body.canUndo
                onClicked: body.undo()
            }
            Button {
                text: qsTr("Redo")
                enabled: body.canRedo
                onClicked: body.redo()
            }
            Label {
                text: !editorVm ? "" : (editorVm.saving ? qsTr("Saving…") : (editorVm.dirty ? qsTr("Unsaved") : qsTr("Saved")))
            }
        }

        TextArea {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            wrapMode: TextArea.Wrap
            textFormat: TextEdit.RichText
            text: editorVm ? editorVm.html : ""
            enabled: editorVm && editorVm.noteId.length > 0
            persistentSelection: true
            onTextChanged: {
                if (!editorVm || !enabled)
                    return
                if (text !== editorVm.html)
                    editorVm.html = text
            }
        }
    }

    function wrapSelection(tag) {
        if (!body || body.selectedText.length === 0)
            return
        const open = "<" + tag + ">"
        const close = "</" + tag + ">"
        body.insert(body.selectionEnd, close)
        body.insert(body.selectionStart, open)
    }

    Connections {
        target: editorVm
        function onHtmlChanged() {
            if (body.text !== editorVm.html)
                body.text = editorVm.html
        }
    }
}
