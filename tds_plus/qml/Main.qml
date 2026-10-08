// language: QML, file: Main.qml, runtime: Qt Quick 6.10, target: Windows 11 desktop
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    objectName: "mainWindow"
    visible: true
    width: 560
    height: 480
    minimumWidth: 380
    minimumHeight: 340
    title: "tds+"
    color: "#09090b"
    font.family: "Geist Mono"
    font.pixelSize: 14
    font.weight: Font.Medium

    readonly property color ink: "#eaeaec"
    readonly property color muted: "#a0a0a9"
    readonly property color dim: "#7d7d87"
    readonly property color accent: "#e6e6e9"
    readonly property color borderColor: "#29292f"
    readonly property bool compact: width < 740
    readonly property bool stackedRows: width < 540
    readonly property int contentInset: compact ? 16 : 28
    readonly property bool motionEnabled: systemMotionEnabled && visible && visibility !== Window.Minimized
    palette.window: "#18181c"
    palette.windowText: ink
    palette.button: "#242429"
    palette.buttonText: ink
    palette.base: "#141418"
    palette.text: ink
    palette.highlight: "#4b4b55"
    palette.highlightedText: ink

    AmbientBackground {
        anchors.fill: parent
        animating: window.motionEnabled
    }

    component Hint: ToolTip {
        id: tip
        padding: 10
        topPadding: 7; bottomPadding: 7
        delay: 750
        contentItem: Text {
            text: tip.text
            color: window.ink
            font.family: window.font.family
            font.pixelSize: 11
        }
        background: Rectangle { color: "#24242a"; border.color: "#3a3a43"; radius: 5 }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: window.motionEnabled ? 120 : 0 } }
        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: window.motionEnabled ? 80 : 0 } }
    }

    component SoftButton: Button {
        id: button
        property bool selected: false
        property bool primary: false
        property bool flatSelection: false
        property string hint: ""
        property string shortcut: ""
        implicitHeight: 34
        leftPadding: 14
        rightPadding: 14
        hoverEnabled: true
        contentItem: RowLayout {
            spacing: 12
            Text {
                text: button.text
                Layout.fillWidth: true
                color: button.primary ? "#151518" : button.selected || button.hovered ? window.ink : window.muted
                font.family: window.font.family
                font.pixelSize: 12
                font.weight: button.primary || button.selected ? Font.DemiBold : Font.Medium
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                opacity: button.enabled ? 1 : 0.5
            }
            Text {
                visible: button.shortcut.length > 0
                text: button.shortcut
                color: button.primary ? "#64646e" : window.dim
                font.pixelSize: 10
            }
        }
        background: Rectangle {
            radius: 6
            color: button.primary ? (button.down ? "#bdbdc6" : button.hovered ? "#fafafa" : window.accent)
                   : button.selected && !button.flatSelection ? "#2b2b32"
                   : (button.down ? "#303036" : button.hovered ? "#232329" : "transparent")
            border.width: button.activeFocus && !button.selected && !button.primary ? 1 : 0
            border.color: "#696972"
            opacity: button.enabled ? 1 : 0.5
            Behavior on color { ColorAnimation { duration: window.motionEnabled ? 160 : 0 } }
        }
        scale: button.down ? 0.98 : 1
        Behavior on scale { NumberAnimation { duration: window.motionEnabled ? 170 : 0; easing.type: Easing.OutCubic } }
        Hint {
            visible: button.hovered && button.hint.length > 0
            text: button.hint
            x: (button.width - width) / 2
            y: button.height + 6
        }
    }

    Shortcut { sequence: "Ctrl+F"; onActivated: search.forceActiveFocus() }
    Shortcut { sequence: "Escape"; enabled: !logPopup.visible && search.text.length > 0; onActivated: search.clear() }

    ColumnLayout {
        id: surface
        anchors.fill: parent
        anchors.margins: window.contentInset
        spacing: window.compact ? 12 : 20
        property real entranceOffset: 8
        opacity: 0
        transform: Translate { y: surface.entranceOffset }
        ParallelAnimation {
            running: true
            NumberAnimation { target: surface; property: "opacity"; to: 1; duration: window.motionEnabled ? 360 : 0; easing.type: Easing.OutCubic }
            NumberAnimation { target: surface; property: "entranceOffset"; to: 0; duration: window.motionEnabled ? 400 : 0; easing.type: Easing.OutCubic }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: window.compact ? 36 : 44
            spacing: window.compact ? 10 : 14
            Text {
                text: "tds+"
                color: window.ink
                font.pixelSize: window.compact ? 22 : 25
                font.weight: Font.DemiBold
                font.letterSpacing: -1.1
            }
            Rectangle { visible: !window.compact; width: 1; height: 18; color: "#37373e"; Layout.leftMargin: 2 }
            Text { visible: !window.compact; text: "Autocast"; color: window.muted; font.pixelSize: 13 }
            Item { Layout.fillWidth: true }
            Rectangle {
                width: 5; height: 5; radius: 3
                color: controlModel.slots > 0 ? window.ink : window.dim
                SequentialAnimation on opacity {
                    running: window.motionEnabled && controlModel.running
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.4; duration: 1000; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1; duration: 1000; easing.type: Easing.InOutSine }
                }
            }
            Text {
                text: controlModel.phase + (controlModel.slots > 0 ? " · " + controlModel.slots + (window.compact ? "" : " active") : "")
                color: window.muted
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.minimumWidth: 0
                Layout.maximumWidth: window.compact ? 140 : 220
            }
            SoftButton {
                objectName: "runButton"
                Layout.preferredWidth: window.compact ? 100 : 112
                Layout.preferredHeight: 34
                Layout.leftMargin: window.compact ? 0 : 6
                primary: !controlModel.running
                text: controlModel.running ? "Pause" : "Start"
                shortcut: "F6"
                enabled: controlModel.ready
                hint: "Toggle autocast · F6"
                onClicked: controlModel.toggle_running()
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#242429" }

        GridLayout {
            Layout.fillWidth: true
            columns: window.compact ? 1 : 2
            columnSpacing: 14
            rowSpacing: 8
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                spacing: 12
                Text {
                    text: "Abilities"
                    color: window.ink
                    font.pixelSize: window.compact ? 18 : 20
                    font.weight: Font.DemiBold
                    font.letterSpacing: -0.5
                }
                Text { text: controlModel.total; color: window.dim; font.pixelSize: 12 }
                Item { Layout.fillWidth: true }
                Rectangle {
                    id: filterGroup
                    Layout.preferredWidth: 148
                    Layout.preferredHeight: 32
                    color: "transparent"
                    radius: 7
                    Rectangle {
                        y: 3; height: parent.height - 6
                        width: (parent.width - 8) / 2
                        x: 3 + (controlModel.liveOnly ? width + 2 : 0)
                        radius: 5
                        color: "#2a2a30"
                        Behavior on x { NumberAnimation { duration: window.motionEnabled ? 230 : 0; easing.type: Easing.OutCubic } }
                    }
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 3; spacing: 2
                        SoftButton {
                            Layout.preferredWidth: 70; Layout.fillWidth: true; Layout.fillHeight: true
                            leftPadding: 7; rightPadding: 7
                            text: "All"; flatSelection: true; selected: !controlModel.liveOnly
                            onClicked: controlModel.liveOnly = false
                        }
                        SoftButton {
                            objectName: "liveFilterButton"
                            Layout.preferredWidth: 70; Layout.fillWidth: true; Layout.fillHeight: true
                            leftPadding: 7; rightPadding: 7
                            text: "In game"
                            flatSelection: true
                            selected: controlModel.liveOnly
                            onClicked: controlModel.liveOnly = true
                        }
                    }
                }
            }
            TextField {
                id: search
                objectName: "searchInput"
                Layout.preferredWidth: 222
                Layout.fillWidth: window.compact
                Layout.preferredHeight: window.compact ? 32 : 36
                leftPadding: 34
                rightPadding: text.length > 0 ? 32 : 56
                placeholderText: "Search abilities"
                placeholderTextColor: window.dim
                color: window.ink
                font.pixelSize: 12
                selectByMouse: true
                selectionColor: "#4b4b55"
                selectedTextColor: window.ink
                onTextChanged: controlModel.query = text
                background: Rectangle {
                    radius: 6
                    color: "#75151519"
                    border.color: search.activeFocus ? "#6c6c76" : window.borderColor
                    Behavior on border.color { ColorAnimation { duration: window.motionEnabled ? 140 : 0 } }
                }
                Canvas {
                    width: 16; height: 16
                    x: 11; anchors.verticalCenter: parent.verticalCenter
                    onPaint: {
                        const context = getContext("2d")
                        context.strokeStyle = "#8b8b95"; context.lineWidth = 1.25
                        context.beginPath(); context.arc(6, 6, 4, 0, Math.PI * 2); context.stroke()
                        context.beginPath(); context.moveTo(9, 9); context.lineTo(13, 13); context.stroke()
                    }
                }
                Text {
                    visible: search.text.length === 0
                    anchors.right: parent.right; anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Ctrl F"; color: window.dim; font.pixelSize: 10
                }
                SoftButton {
                    visible: search.text.length > 0
                    width: 26; height: 26
                    anchors.right: parent.right; anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: 0; rightPadding: 0
                    text: "×"; hint: "Clear search"
                    onClicked: search.clear()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 92
            color: "#a6111115"
            radius: 9
            border.color: window.borderColor
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0
                RowLayout {
                    visible: !window.stackedRows
                    Layout.fillWidth: true
                    Layout.preferredHeight: window.compact ? 32 : 38
                    Layout.leftMargin: window.compact ? 14 : 20; Layout.rightMargin: window.compact ? 14 : 20
                    spacing: window.compact ? 10 : 12
                    Text { text: "Ability"; color: window.dim; font.pixelSize: 11; Layout.fillWidth: true }
                    Text { visible: !window.compact; text: "Cooldown"; color: window.dim; font.pixelSize: 11; Layout.preferredWidth: 78 }
                    Text { visible: !window.compact; text: "Interval"; color: window.dim; font.pixelSize: 11; Layout.preferredWidth: 96 }
                    Text { text: "Mode"; color: window.dim; font.pixelSize: 11; Layout.preferredWidth: 196 }
                }
                Rectangle { visible: !window.stackedRows; Layout.fillWidth: true; height: 1; color: window.borderColor }
                ListView {
                    id: abilityList
                    objectName: "abilityList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: controlModel
                    boundsBehavior: Flickable.StopAtBounds
                    reuseItems: true
                    cacheBuffer: 128
                    ScrollBar.vertical: ScrollBar {
                        width: 5
                        policy: ScrollBar.AsNeeded
                        contentItem: Rectangle {
                            implicitWidth: 3; radius: 2
                            color: parent.pressed ? "#93939e" : "#4a4a54"
                            opacity: parent.active ? 1 : 0.45
                        }
                    }
                    delegate: Rectangle {
                        id: abilityRow
                        required property string abilityKey
                        required property string abilityName
                        required property string towerName
                        required property real cooldown
                        required property int abilityMode
                        required property int chainSeconds
                        required property bool isLive
                        width: ListView.view.width
                        height: window.stackedRows ? 96 : window.compact ? 60 : 64
                        color: rowHover.hovered ? "#7325252b" : "transparent"
                        Behavior on color { ColorAnimation { duration: window.motionEnabled ? 180 : 0 } }
                        HoverHandler { id: rowHover }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left; anchors.right: parent.right
                            anchors.leftMargin: window.compact ? 14 : 20; anchors.rightMargin: window.compact ? 14 : 20
                            height: 1; color: "#242429"
                        }
                        GridLayout {
                            anchors.fill: parent
                            anchors.leftMargin: window.compact ? 14 : 20; anchors.rightMargin: window.compact ? 14 : 20
                            anchors.topMargin: window.stackedRows ? 10 : 0
                            anchors.bottomMargin: window.stackedRows ? 10 : 0
                            columns: window.stackedRows ? 2 : window.compact ? 3 : 4
                            columnSpacing: window.compact ? 10 : 12
                            rowSpacing: 6
                            ColumnLayout {
                                Layout.row: 0; Layout.column: 0
                                Layout.columnSpan: window.stackedRows ? 2 : 1
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.preferredWidth: 1
                                spacing: 4
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        objectName: "label_" + abilityRow.abilityKey
                                        text: abilityRow.abilityName
                                        color: abilityRow.abilityMode === 2 ? window.muted : window.ink
                                        font.pixelSize: 14; font.weight: Font.DemiBold
                                        elide: Text.ElideRight; Layout.fillWidth: true
                                    }
                                    Rectangle { visible: abilityRow.isLive; width: 4; height: 4; radius: 2; color: window.accent }
                                }
                                Text {
                                    text: (abilityRow.towerName || "Other") + (window.compact && abilityRow.cooldown > 0 ? " · " + abilityRow.cooldown + "s" : "")
                                    color: window.dim; font.pixelSize: 11
                                    elide: Text.ElideRight; Layout.fillWidth: true
                                    HoverHandler { id: compactCooldownHover }
                                    Hint { visible: window.compact && compactCooldownHover.hovered; text: "Wiki cooldown" }
                                }
                            }
                            Text {
                                visible: !window.compact
                                Layout.row: 0; Layout.column: 1
                                Layout.preferredWidth: 78
                                text: abilityRow.cooldown > 0 ? abilityRow.cooldown + "s" : "—"
                                color: window.muted
                                font.pixelSize: 12
                                font.features: { "tnum": 1 }
                                HoverHandler { id: cooldownHover }
                                Hint { visible: cooldownHover.hovered; text: "Wiki cooldown" }
                            }
                            Item {
                                visible: !window.compact || abilityRow.abilityMode === 1
                                Layout.row: window.stackedRows ? 1 : 0
                                Layout.column: window.stackedRows ? 0 : window.compact ? 1 : 2
                                Layout.fillWidth: window.stackedRows
                                Layout.preferredWidth: window.compact ? 84 : 96
                                Layout.preferredHeight: 32
                                Text {
                                    visible: abilityRow.abilityMode !== 1
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "—"
                                    color: window.dim
                                    font.pixelSize: 12
                                }
                                SpinBox {
                                    id: interval
                                    objectName: "interval_" + abilityRow.abilityKey
                                    visible: abilityRow.abilityMode === 1
                                    width: 84; height: 32
                                    from: 1; to: 600
                                    value: abilityRow.chainSeconds
                                    editable: true
                                    Accessible.name: "Chain interval in seconds"
                                    onValueModified: controlModel.set_seconds(abilityRow.abilityKey, value)
                                    textFromValue: function(value, locale) { return value.toString() }
                                    valueFromText: function(text, locale) { return parseInt(text) || 10 }
                                    contentItem: TextInput {
                                        text: interval.textFromValue(interval.value, interval.locale)
                                        color: window.ink
                                        font.pixelSize: 12
                                        font.features: { "tnum": 1 }
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        rightPadding: 26
                                        selectByMouse: true
                                        readOnly: !interval.editable
                                        validator: interval.validator
                                        inputMethodHints: Qt.ImhDigitsOnly
                                    }
                                    background: Rectangle {
                                        radius: 5; color: "#1c1c22"
                                        border.color: interval.activeFocus ? "#6c6c76" : "#3a3a42"
                                    }
                                    Text { x: 48; anchors.verticalCenter: parent.verticalCenter; text: "s"; color: window.dim; font.pixelSize: 11 }
                                    up.indicator: Rectangle {
                                        x: interval.width - width; y: 1; width: 22; height: 15
                                        color: interval.up.pressed ? "#37373f" : "transparent"
                                        Text { anchors.centerIn: parent; text: "+"; color: window.muted; font.pixelSize: 11 }
                                    }
                                    down.indicator: Rectangle {
                                        x: interval.width - width; y: interval.height - height - 1; width: 22; height: 15
                                        color: interval.down.pressed ? "#37373f" : "transparent"
                                        Text { anchors.centerIn: parent; text: "−"; color: window.muted; font.pixelSize: 11 }
                                    }
                                }
                            }
                            Rectangle {
                                id: modeGroup
                                Layout.row: window.stackedRows ? 1 : 0
                                Layout.column: window.stackedRows ? 1 : window.compact ? 2 : 3
                                Layout.preferredWidth: 196
                                Layout.minimumWidth: 196
                                Layout.maximumWidth: 196
                                Layout.preferredHeight: 32
                                Layout.alignment: Qt.AlignRight
                                color: "transparent"
                                radius: 6
                                Rectangle {
                                    y: 3; height: parent.height - 6
                                    width: (parent.width - 10) / 3
                                    x: 3 + abilityRow.abilityMode * (width + 2)
                                    radius: 5
                                    color: "#2a2a31"
                                    border.color: "#3a3a43"
                                    Behavior on x { NumberAnimation { duration: window.motionEnabled ? 230 : 0; easing.type: Easing.OutCubic } }
                                }
                                RowLayout {
                                    anchors.fill: parent; anchors.margins: 3; spacing: 2
                                    SoftButton {
                                        objectName: "mode_auto_" + abilityRow.abilityKey
                                        Layout.preferredWidth: 62; Layout.fillWidth: true; Layout.fillHeight: true
                                        flatSelection: true
                                        leftPadding: 7; rightPadding: 7
                                        text: "Auto"; selected: abilityRow.abilityMode === 0
                                        hint: "Cast as soon as the ability is ready"
                                        onClicked: controlModel.set_mode(abilityRow.abilityKey, 0)
                                    }
                                    SoftButton {
                                        objectName: "mode_chain_" + abilityRow.abilityKey
                                        Layout.preferredWidth: 62; Layout.fillWidth: true; Layout.fillHeight: true
                                        flatSelection: true
                                        leftPadding: 7; rightPadding: 7
                                        text: "Chain"; selected: abilityRow.abilityMode === 1
                                        hint: "Space out casts within a group"
                                        onClicked: controlModel.set_mode(abilityRow.abilityKey, 1)
                                    }
                                    SoftButton {
                                        objectName: "mode_off_" + abilityRow.abilityKey
                                        Layout.preferredWidth: 62; Layout.fillWidth: true; Layout.fillHeight: true
                                        flatSelection: true
                                        leftPadding: 7; rightPadding: 7
                                        text: "Off"; selected: abilityRow.abilityMode === 2
                                        hint: "Disable autocast for this ability"
                                        onClicked: controlModel.set_mode(abilityRow.abilityKey, 2)
                                    }
                                }
                            }
                        }
                    }
                    Column {
                        visible: controlModel.count === 0
                        anchors.centerIn: parent
                        width: parent.width - 32
                        spacing: 8
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: search.text.length > 0 ? "No results" : "No abilities in game"
                            color: window.ink; font.pixelSize: 14; font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: search.text.length > 0 ? "Try another name." : "Abilities appear when a match is connected."
                            color: window.dim; font.pixelSize: 12
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            Text {
                Layout.fillWidth: true
                text: controlModel.saveStatus.length > 0 ? controlModel.saveStatus : "Changes save automatically"
                color: controlModel.saveError ? "#d9a3ac" : window.dim
                font.pixelSize: 11
                elide: Text.ElideRight
                Behavior on color { ColorAnimation { duration: window.motionEnabled ? 160 : 0 } }
            }
            SoftButton {
                objectName: "logButton"
                implicitHeight: 24
                leftPadding: 8; rightPadding: 8
                text: "Log"
                onClicked: logPopup.open()
            }
        }
    }

    Popup {
        id: logPopup
        objectName: "activityPopup"
        x: window.contentInset
        y: window.height - height - 48
        width: window.width - window.contentInset * 2
        height: Math.min(260, window.height * 0.5)
        padding: 18
        focus: true
        modal: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: window.motionEnabled ? 180 : 0 }
                NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: window.motionEnabled ? 230 : 0; easing.type: Easing.OutCubic }
            }
        }
        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: window.motionEnabled ? 130 : 0 } }
        background: Rectangle { color: "#19191e"; border.color: "#3c3c45"; radius: 9 }
        ColumnLayout {
            anchors.fill: parent
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Activity"; color: window.ink; font.pixelSize: 15; font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                SoftButton { objectName: "closeLogButton"; text: "Close"; onClicked: logPopup.close() }
            }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true
                TextArea {
                    text: controlModel.logLines.join("\n")
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    color: "#acacb8"
                    font.family: window.font.family
                    font.pixelSize: 11
                    background: null
                }
            }
        }
    }
}
