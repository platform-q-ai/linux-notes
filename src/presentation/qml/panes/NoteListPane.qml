import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var noteListVm: null
    property var requestDeleteNote: null

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Label {
                text: {
                    if (root.noteListVm && root.noteListVm.searching)
                        return qsTr("Search results")
                    return qsTr("Notes")
                }
                font.bold: true
                Layout.fillWidth: true
            }
            ToolButton {
                text: qsTr("New")
                onClicked: if (root.noteListVm) root.noteListVm.createNote()
            }
            ToolButton {
                text: qsTr("Del")
                enabled: !!(root.noteListVm && root.noteListVm.selectedNoteId
                            && root.noteListVm.selectedNoteId.length > 0)
                onClicked: {
                    if (!root.noteListVm)
                        return
                    var id = root.noteListVm.selectedNoteId
                    if (typeof root.requestDeleteNote === "function")
                        root.requestDeleteNote(id)
                    else
                        root.noteListVm.deleteNote(id)
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
}
