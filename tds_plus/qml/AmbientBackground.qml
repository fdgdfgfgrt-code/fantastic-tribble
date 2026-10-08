// language: QML, file: AmbientBackground.qml, runtime: Qt Quick 6.10, target: native animated backdrop
import QtQuick

Item {
    id: ambient
    objectName: "ambientBackground"
    property bool animating: true
    property real elapsed: 0
    property real pointerX: width * 0.7
    property real pointerY: height * 0.2
    property real pointerPresence: 0
    readonly property bool shaderReady: field.status === ShaderEffect.Compiled

    HoverHandler {
        id: pointerTracker
        parent: ambient.parent
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    }

    FrameAnimation {
        running: ambient.animating
        onTriggered: {
            const delta = Math.min(frameTime, 0.05)
            const follow = 1 - Math.exp(-delta * 5)
            ambient.elapsed += delta
            ambient.pointerPresence += ((pointerTracker.hovered ? 1 : 0) - ambient.pointerPresence) * follow
            if (pointerTracker.hovered) {
                ambient.pointerX += (pointerTracker.point.position.x - ambient.pointerX) * follow
                ambient.pointerY += (pointerTracker.point.position.y - ambient.pointerY) * follow
            }
        }
    }

    ShaderEffect {
        id: field
        objectName: "ambientShader"
        anchors.fill: parent
        blending: false
        property real time: ambient.elapsed
        property vector2d resolution: Qt.vector2d(width, height)
        property vector2d pointer: Qt.vector2d(ambient.pointerX / Math.max(width, 1), ambient.pointerY / Math.max(height, 1))
        property real pointer_presence: ambient.pointerPresence
        fragmentShader: "qrc:/shaders/ambient.frag.qsb"
        layer.enabled: true
        layer.smooth: true
        layer.textureSize: Qt.size(Math.max(1, Math.ceil(width / 2)), Math.max(1, Math.ceil(height / 2)))
    }
}
