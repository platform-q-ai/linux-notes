import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    property string saveState: "empty"
    property string errorString: ""

    implicitHeight: 24
    implicitWidth: label.implicitWidth + 16
    radius: 12
    color: {
        switch (saveState) {
        case "saving": return "#fff3cd"
        case "dirty": return "#e7f1ff"
        case "error": return "#f8d7da"
        case "saved": return "#d1e7dd"
        case "loading": return "#e2e3e5"
        default: return "#f8f9fa"
        }
    }
    border.color: Qt.darker(color, 1.2)
    border.width: 1

    Label {
        id: label
        anchors.centerIn: parent
        font.pixelSize: 12
        text: {
            switch (saveState) {
            case "saving": return qsTr("Saving…")
            case "dirty": return qsTr("Unsaved")
            case "error": return errorString.length ? errorString : qsTr("Save failed")
            case "saved": return qsTr("Saved")
            case "loading": return qsTr("Loading…")
            default: return qsTr("No note")
            }
        }
        elide: Text.ElideRight
        width: Math.min(implicitWidth, 220)
    }

    ToolTip.visible: saveState === "error" && errorString.length > 0 && hover.hovered
    ToolTip.text: errorString
    HoverHandler { id: hover }
}
