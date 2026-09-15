import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: root
    property var folderVm

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Label { text: qsTr("Folders"); font.bold: true; Layout.fillWidth: true }
            Button {
                text: qsTr("+")
                onClicked: if (folderVm) folderVm.createFolder(qsTr("New Folder"))
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: folderVm ? folderVm.model : null
            currentIndex: -1
            delegate: ItemDelegate {
                width: list.width
                text: model.name
                highlighted: folderVm && model.folderId === folderVm.selectedFolderId
                onClicked: if (folderVm) folderVm.selectedFolderId = model.folderId
            }
        }
    }
}
