// language: QML, file: TitleBar.qml, runtime: Qt Quick 6.10, target: Windows 11 desktop
// Caption buttons of the frameless window. Dragging, double-click maximise, resizing and the
// system menu come from the native frame (see qt/frame_win.hpp); this item only draws the
// minimise / maximise / close buttons and the area they sit in.
import QtQuick

Item {
    id: bar

    required property var appWindow
    property color ink: "#eaeaec"
    property color muted: "#a0a0a9"
    property color dim: "#7d7d87"
    property bool animated: true

    readonly property int buttonWidth: 46
    readonly property int controlsWidth: buttonWidth * 3
    readonly property bool maximized: appWindow.visibility === Window.Maximized
    readonly property bool windowActive: appWindow.active

    height: 36

    component CaptionButton: Item {
        id: button

        property string kind: "close"        // minimize | maximize | close
        property string label: ""
        readonly property bool isClose: kind === "close"
        readonly property bool hovered: area.containsMouse
        readonly property bool pressed: area.pressed
        signal clicked()

        width: bar.buttonWidth
        height: bar.height

        Rectangle {
            anchors.fill: parent
            color: button.isClose
                   ? (button.pressed ? "#a82a1d" : button.hovered ? "#c42b1c" : "transparent")
                   : (button.pressed ? "#2f2f36" : button.hovered ? "#26262c" : "transparent")
            Behavior on color { ColorAnimation { duration: bar.animated ? 120 : 0 } }
        }

        Item {
            id: glyph
            width: 10
            height: 10
            anchors.centerIn: parent
            property color tone: button.isClose && button.hovered ? "#ffffff"
                                         : button.hovered || button.pressed ? bar.ink
                                         : bar.windowActive ? bar.muted : bar.dim
            Behavior on tone { ColorAnimation { duration: bar.animated ? 120 : 0 } }

            Rectangle {
                visible: button.kind === "minimize"
                y: 5; width: 10; height: 1
                color: glyph.tone
            }
            Rectangle {
                visible: button.kind === "maximize" && !bar.maximized
                anchors.fill: parent
                color: "transparent"
                border.width: 1
                border.color: glyph.tone
            }
            Item {
                visible: button.kind === "maximize" && bar.maximized
                anchors.fill: parent
                Rectangle { x: 0; y: 2; width: 8; height: 8; color: "transparent"; border.width: 1; border.color: glyph.tone }
                Rectangle { x: 2; y: 0; width: 1; height: 2; color: glyph.tone }
                Rectangle { x: 2; y: 0; width: 8; height: 1; color: glyph.tone }
                Rectangle { x: 9; y: 0; width: 1; height: 8; color: glyph.tone }
                Rectangle { x: 8; y: 7; width: 2; height: 1; color: glyph.tone }
            }
            Item {
                visible: button.isClose
                anchors.fill: parent
                Rectangle { width: 14; height: 1; anchors.centerIn: parent; rotation: 45; antialiasing: true; color: glyph.tone }
                Rectangle { width: 14; height: 1; anchors.centerIn: parent; rotation: -45; antialiasing: true; color: glyph.tone }
            }
        }

        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            onClicked: button.clicked()
        }

        Accessible.role: Accessible.Button
        Accessible.name: button.label
        Accessible.onPressAction: button.clicked()
    }

    Row {
        anchors.top: parent.top
        anchors.right: parent.right
        CaptionButton {
            objectName: "minimizeButton"
            kind: "minimize"
            label: "Minimize"
            onClicked: bar.appWindow.showMinimized()
        }
        CaptionButton {
            objectName: "maximizeButton"
            kind: "maximize"
            label: bar.maximized ? "Restore" : "Maximize"
            onClicked: bar.maximized ? bar.appWindow.showNormal() : bar.appWindow.showMaximized()
        }
        CaptionButton {
            objectName: "closeButton"
            kind: "close"
            label: "Close"
            onClicked: bar.appWindow.close()
        }
    }
}
