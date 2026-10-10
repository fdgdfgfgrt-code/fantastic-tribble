// language: QML, file: Main.qml, runtime: Qt Quick 6.10, target: Windows 11 desktop
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: window
    objectName: "mainWindow"
    visible: false  // shown from C++ once the size is restored, unless it starts in the tray
    width: 560
    height: customFrameEnabled ? 516 : 480
    minimumWidth: 380
    minimumHeight: customFrameEnabled ? 376 : 340
    title: "tds+"
    color: p.window
    flags: customFrame ? (Qt.Window | Qt.FramelessWindowHint) : Qt.Window
    font.family: "Geist Mono"
    font.pixelSize: 14
    font.weight: Font.Medium

    // every color comes from the theme in the settings (qt/themes.hpp)
    readonly property var p: appSettings.palette
    readonly property color ink: p.ink
    readonly property color muted: p.muted
    readonly property color dim: p.dim
    readonly property color accent: p.accent
    readonly property color borderColor: p.border
    readonly property bool compact: width < 740
    readonly property bool stackedRows: width < 540
    readonly property int contentInset: compact ? 16 : 28
    readonly property bool customFrame: customFrameEnabled
    readonly property int titleBarHeight: customFrame ? titleBar.height : 0
    readonly property int captionControlsWidth: titleBar.controlsWidth
    readonly property bool motionEnabled: systemMotionEnabled && visible && visibility !== Window.Minimized
    palette.window: p.popup
    palette.windowText: ink
    palette.button: p.raised
    palette.buttonText: ink
    palette.base: p.input
    palette.text: ink
    palette.highlight: p.selection
    palette.highlightedText: ink

    property bool entered: false
    onVisibleChanged: if (visible && !entered) { entered = true; entrance.start() }
    onClosing: (close) => {
        if (appSettings.closeToTray && !windowControl.quitting && windowControl.hideToTray())
            close.accepted = false
    }
    onVisibilityChanged: (visibility) => {
        if (visibility === Window.Minimized && appSettings.minimizeToTray) windowControl.hideToTray()
    }

    Item {
        id: backdrop
        objectName: "backdrop"
        anchors.fill: parent
        readonly property bool picture: appSettings.backgroundMode === "picture" && appSettings.hasBackgroundFile
        AmbientBackground {
            anchors.fill: parent
            visible: appSettings.backgroundMode === "waves"
            animating: window.motionEnabled && visible
            baseColor: window.p.waveBase
            tintColor: window.p.waveTint
        }
        Image {
            id: backgroundImage
            objectName: "backgroundImage"
            anchors.fill: parent
            visible: backdrop.picture && !appSettings.backgroundAnimated
            source: visible ? appSettings.backgroundUrl : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: false
            smooth: true
            mipmap: true
        }
        AnimatedImage {
            id: backgroundAnimation
            objectName: "backgroundAnimation"
            anchors.fill: parent
            visible: backdrop.picture && appSettings.backgroundAnimated
            source: visible ? appSettings.backgroundUrl : ""
            fillMode: Image.PreserveAspectCrop
            cache: false
            smooth: true
            playing: visible && window.visible && window.visibility !== Window.Minimized
        }
        // keeps text readable on any picture: the theme's window color laid over it
        Rectangle {
            objectName: "backgroundShade"
            anchors.fill: parent
            visible: backdrop.picture
            color: window.p.window
            opacity: appSettings.backgroundDim / 100
        }
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
        background: Rectangle { color: window.p.raised; border.color: window.p.borderStrong; radius: 5 }
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
                color: button.primary ? window.p.onAccent : button.selected || button.hovered ? window.ink : window.muted
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
                color: button.primary ? window.p.onAccentDim : window.dim
                font.pixelSize: 10
            }
        }
        background: Rectangle {
            radius: 6
            color: button.primary ? (button.down ? window.p.accentDown : button.hovered ? window.p.accentHover : window.accent)
                   : button.selected && !button.flatSelection ? window.p.control
                   : (button.down ? window.p.controlDown : button.hovered ? window.p.controlHover : "transparent")
            border.width: button.visualFocus && !button.selected && !button.primary ? 1 : 0  // keyboard focus only
            border.color: window.p.focus
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
    Shortcut { sequence: "Escape"; enabled: !logPopup.visible && !settingsPopup.visible && search.text.length > 0; onActivated: search.clear() }
    Shortcut { sequence: "Ctrl+,"; onActivated: settingsPopup.opened ? settingsPopup.close() : settingsPopup.open() }

    ColumnLayout {
        id: surface
        anchors.fill: parent
        anchors.margins: window.contentInset
        anchors.topMargin: window.customFrame ? window.titleBarHeight + (window.compact ? 4 : 10) : window.contentInset
        spacing: window.compact ? 12 : 20
        property real entranceOffset: 8
        opacity: 0
        transform: Translate { y: surface.entranceOffset }
        ParallelAnimation {
            id: entrance
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
            Rectangle { visible: !window.compact; width: 1; height: 18; color: window.p.separator; Layout.leftMargin: 2 }
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

        Rectangle { Layout.fillWidth: true; height: 1; color: window.p.divider }

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
                        color: window.p.thumb
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
                selectionColor: window.p.selection
                selectedTextColor: window.ink
                onTextChanged: controlModel.query = text
                background: Rectangle {
                    radius: 6
                    color: window.p.field
                    border.color: search.activeFocus ? window.p.focus : window.borderColor
                    Behavior on border.color { ColorAnimation { duration: window.motionEnabled ? 140 : 0 } }
                }
                Canvas {
                    width: 16; height: 16
                    x: 11; anchors.verticalCenter: parent.verticalCenter
                    property color stroke: window.muted
                    onStrokeChanged: requestPaint()
                    onPaint: {
                        const context = getContext("2d")
                        context.reset()
                        context.strokeStyle = stroke; context.lineWidth = 1.25
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
            color: window.p.surface
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
                            color: parent.pressed ? window.p.scrollActive : window.p.scroll
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
                        color: rowHover.hovered ? window.p.rowHover : "transparent"
                        Behavior on color { ColorAnimation { duration: window.motionEnabled ? 180 : 0 } }
                        HoverHandler { id: rowHover }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left; anchors.right: parent.right
                            anchors.leftMargin: window.compact ? 14 : 20; anchors.rightMargin: window.compact ? 14 : 20
                            height: 1; color: window.p.divider
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
                                        radius: 5; color: window.p.input
                                        border.color: interval.activeFocus ? window.p.focus : window.p.borderStrong
                                    }
                                    Text { x: 48; anchors.verticalCenter: parent.verticalCenter; text: "s"; color: window.muted; font.pixelSize: 11 }
                                    up.indicator: Rectangle {
                                        x: interval.width - width; y: 1; width: 22; height: 15
                                        color: interval.up.pressed ? window.p.controlDown : "transparent"
                                        Text { anchors.centerIn: parent; text: "+"; color: window.muted; font.pixelSize: 11 }
                                    }
                                    down.indicator: Rectangle {
                                        x: interval.width - width; y: interval.height - height - 1; width: 22; height: 15
                                        color: interval.down.pressed ? window.p.controlDown : "transparent"
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
                                    color: window.p.thumb
                                    border.color: window.p.borderStrong
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
                color: controlModel.saveError ? window.p.danger : window.dim
                font.pixelSize: 11
                elide: Text.ElideRight
                Behavior on color { ColorAnimation { duration: window.motionEnabled ? 160 : 0 } }
            }
            SoftButton {
                objectName: "settingsButton"
                implicitHeight: 24
                leftPadding: 8; rightPadding: 8
                text: "Settings"
                hint: "Theme, background, tray · Ctrl+,"
                onClicked: settingsPopup.open()
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
        background: Rectangle { color: window.p.popup; border.color: window.p.popupBorder; radius: 9 }
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
                    color: window.muted
                    font.family: window.font.family
                    font.pixelSize: 11
                    background: null
                }
            }
        }
    }

    component SectionLabel: Text {
        color: window.dim
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.letterSpacing: 0.6
        Layout.topMargin: 10
    }

    // a switch whose state always follows the setting: clicks ask for a change instead of flipping it
    component OptionSwitch: Switch {
        id: option
        property string detail: ""
        Layout.fillWidth: true
        checkable: false
        hoverEnabled: true
        leftPadding: 0; rightPadding: 0; topPadding: 6; bottomPadding: 6
        indicator: Rectangle {
            x: option.width - width
            y: (option.height - height) / 2
            implicitWidth: 36; implicitHeight: 20; radius: 10
            opacity: option.enabled ? 1 : 0.45
            color: option.checked ? window.accent : option.hovered ? window.p.controlHover : window.p.control
            border.color: option.checked ? window.accent : option.visualFocus ? window.p.focus : window.p.borderStrong
            Behavior on color { ColorAnimation { duration: window.motionEnabled ? 140 : 0 } }
            Rectangle {
                width: 14; height: 14; radius: 7; y: 3
                x: option.checked ? parent.width - width - 3 : 3
                color: option.checked ? window.p.onAccent : window.muted
                Behavior on x { NumberAnimation { duration: window.motionEnabled ? 160 : 0; easing.type: Easing.OutCubic } }
            }
        }
        contentItem: ColumnLayout {
            spacing: 2
            Text {
                text: option.text
                color: window.ink
                opacity: option.enabled ? 1 : 0.5
                font.pixelSize: 13
                elide: Text.ElideRight
                Layout.fillWidth: true; Layout.rightMargin: 48
            }
            Text {
                visible: option.detail.length > 0
                text: option.detail
                color: window.dim
                font.pixelSize: 11
                wrapMode: Text.Wrap
                Layout.fillWidth: true; Layout.rightMargin: 48
            }
        }
        background: Item {}
    }

    FileDialog {
        id: backgroundDialog
        title: "Choose a background picture"
        nameFilters: ["Pictures (*.png *.jpg *.jpeg *.bmp *.gif)", "All files (*)"]
        onAccepted: appSettings.importBackground(selectedFile)
    }

    Popup {
        id: settingsPopup
        objectName: "settingsPopup"
        parent: Overlay.overlay
        width: Math.min(window.width - 24, 560)
        height: Math.min(window.height - window.titleBarHeight - 16, 660)
        x: Math.round((window.width - width) / 2)
        y: window.titleBarHeight + Math.max(8, Math.round((window.height - window.titleBarHeight - height) / 2))
        padding: 0
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: appSettings.clearError()
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, window.p.dark ? 0.45 : 0.22) }
        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: window.motionEnabled ? 180 : 0 }
                NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: window.motionEnabled ? 230 : 0; easing.type: Easing.OutCubic }
            }
        }
        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: window.motionEnabled ? 130 : 0 } }
        background: Rectangle { color: window.p.popup; border.color: window.p.popupBorder; radius: 10 }

        contentItem: ColumnLayout {
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 20; Layout.rightMargin: 12
                Layout.preferredHeight: 52
                Text { text: "Settings"; color: window.ink; font.pixelSize: 16; font.weight: Font.DemiBold; Layout.fillWidth: true }
                SoftButton { objectName: "closeSettingsButton"; text: "Done"; shortcut: "Esc"; onClicked: settingsPopup.close() }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: window.p.divider }

            ScrollView {
                id: settingsScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: settingsScroll.availableWidth
                    spacing: 8

                    Item { Layout.preferredHeight: 4 }
                    SectionLabel { text: "THEME"; Layout.leftMargin: 20 }
                    Flow {
                        Layout.fillWidth: true
                        Layout.leftMargin: 20; Layout.rightMargin: 20
                        spacing: 10
                        Repeater {
                            model: appSettings.themes
                            delegate: AbstractButton {
                                id: themeCard
                                required property var modelData
                                objectName: "theme_" + modelData.id
                                readonly property bool current: appSettings.theme === modelData.id
                                width: 112; height: 88
                                hoverEnabled: true
                                Accessible.role: Accessible.RadioButton
                                Accessible.name: modelData.name + " theme"
                                Accessible.checked: current
                                onClicked: appSettings.theme = modelData.id
                                contentItem: Item {
                                    Rectangle {
                                        width: parent.width; height: 60; radius: 8
                                        color: themeCard.modelData.window
                                        border.width: themeCard.current ? 2 : 1
                                        border.color: themeCard.current ? window.accent
                                                     : themeCard.hovered || themeCard.visualFocus ? window.p.focus : window.p.border
                                        Behavior on border.color { ColorAnimation { duration: window.motionEnabled ? 140 : 0 } }
                                        Rectangle {
                                            x: 10; y: 10; width: parent.width - 20; height: 28; radius: 5
                                            color: themeCard.modelData.panel
                                            border.color: themeCard.modelData.border
                                            Rectangle { x: 8; y: 7; width: 40; height: 4; radius: 2; color: themeCard.modelData.ink }
                                            Rectangle { x: 8; y: 16; width: 26; height: 4; radius: 2; color: themeCard.modelData.muted }
                                            Rectangle {
                                                anchors.right: parent.right; anchors.rightMargin: 7
                                                anchors.verticalCenter: parent.verticalCenter
                                                width: 20; height: 12; radius: 3
                                                color: themeCard.modelData.accent
                                            }
                                        }
                                        Rectangle { x: 10; y: 45; width: 34; height: 4; radius: 2; color: themeCard.modelData.muted; opacity: 0.6 }
                                    }
                                    Text {
                                        y: 66
                                        text: themeCard.modelData.name
                                        color: themeCard.current ? window.ink : window.muted
                                        font.pixelSize: 12
                                        font.weight: themeCard.current ? Font.DemiBold : Font.Medium
                                    }
                                }
                                background: Item {}
                            }
                        }
                    }

                    SectionLabel { text: "ACCENT"; Layout.leftMargin: 20 }
                    Flow {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16; Layout.rightMargin: 20
                        spacing: 6
                        Repeater {
                            model: appSettings.accents
                            delegate: AbstractButton {
                                id: swatch
                                required property var modelData
                                objectName: "accent_" + modelData.id
                                readonly property bool current: appSettings.accent === modelData.id
                                width: 32; height: 32
                                hoverEnabled: true
                                Accessible.role: Accessible.RadioButton
                                Accessible.name: modelData.name + " accent"
                                Accessible.checked: current
                                onClicked: appSettings.accent = modelData.id
                                contentItem: Item {}
                                background: Item {
                                    Rectangle {
                                        anchors.fill: parent; radius: width / 2
                                        color: "transparent"
                                        border.width: 2
                                        border.color: swatch.current ? window.ink : swatch.visualFocus ? window.p.focus : "transparent"
                                    }
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 22; height: 22; radius: 11
                                        color: swatch.modelData.color
                                        border.color: window.p.borderStrong
                                        scale: swatch.hovered ? 1.08 : 1
                                        Behavior on scale { NumberAnimation { duration: window.motionEnabled ? 120 : 0 } }
                                    }
                                }
                                Hint { visible: swatch.hovered; text: swatch.modelData.name; x: (swatch.width - width) / 2; y: swatch.height + 4 }
                            }
                        }
                    }

                    SectionLabel { text: "BACKGROUND"; Layout.leftMargin: 20 }
                    Rectangle {
                        id: backgroundGroup
                        Layout.leftMargin: 20; Layout.rightMargin: 20
                        Layout.fillWidth: true
                        Layout.maximumWidth: 330
                        Layout.preferredHeight: 34
                        radius: 7
                        color: window.p.input
                        border.color: window.p.border
                        readonly property int index: appSettings.backgroundMode === "solid" ? 1 : appSettings.backgroundMode === "picture" ? 2 : 0
                        Rectangle {
                            y: 3; height: parent.height - 6
                            width: (parent.width - 10) / 3
                            x: 3 + backgroundGroup.index * (width + 2)
                            radius: 5
                            color: window.p.thumb
                            border.color: window.p.borderStrong
                            Behavior on x { NumberAnimation { duration: window.motionEnabled ? 230 : 0; easing.type: Easing.OutCubic } }
                        }
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 3; spacing: 2
                            SoftButton {
                                objectName: "background_waves"
                                Layout.fillWidth: true; Layout.fillHeight: true
                                flatSelection: true; selected: backgroundGroup.index === 0
                                text: "Waves"; hint: "Animated light, tinted by the theme"
                                onClicked: appSettings.backgroundMode = "waves"
                            }
                            SoftButton {
                                objectName: "background_solid"
                                Layout.fillWidth: true; Layout.fillHeight: true
                                flatSelection: true; selected: backgroundGroup.index === 1
                                text: "Solid"; hint: "Plain theme color, no animation"
                                onClicked: appSettings.backgroundMode = "solid"
                            }
                            SoftButton {
                                objectName: "background_picture"
                                Layout.fillWidth: true; Layout.fillHeight: true
                                flatSelection: true; selected: backgroundGroup.index === 2
                                text: "Picture"; hint: "Your photo or an animated GIF"
                                onClicked: appSettings.hasBackgroundFile ? appSettings.backgroundMode = "picture" : backgroundDialog.open()
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 20; Layout.rightMargin: 20
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            text: appSettings.hasBackgroundFile
                                  ? appSettings.backgroundName + (appSettings.backgroundAnimated ? "  ·  animated" : "")
                                  : "PNG, JPG, BMP or GIF up to 40 MB"
                            color: appSettings.hasBackgroundFile ? window.muted : window.dim
                            font.pixelSize: 12
                            elide: Text.ElideMiddle
                        }
                        SoftButton {
                            objectName: "chooseBackgroundButton"
                            text: appSettings.hasBackgroundFile ? "Change…" : "Choose picture…"
                            onClicked: backgroundDialog.open()
                        }
                        SoftButton {
                            objectName: "removeBackgroundButton"
                            visible: appSettings.hasBackgroundFile
                            text: "Remove"
                            onClicked: appSettings.clearBackground()
                        }
                    }
                    RowLayout {
                        visible: appSettings.backgroundMode === "picture"
                        Layout.fillWidth: true
                        Layout.leftMargin: 20; Layout.rightMargin: 20
                        spacing: 12
                        Text { text: "Darken"; color: window.muted; font.pixelSize: 12 }
                        Slider {
                            id: dimSlider
                            objectName: "backgroundDimSlider"
                            Layout.fillWidth: true
                            from: 0; to: 90; stepSize: 5
                            snapMode: Slider.SnapAlways
                            value: appSettings.backgroundDim
                            onMoved: appSettings.backgroundDim = Math.round(value)
                            Accessible.name: "Darken the background picture"
                            background: Rectangle {
                                x: dimSlider.leftPadding
                                y: dimSlider.topPadding + dimSlider.availableHeight / 2 - height / 2
                                width: dimSlider.availableWidth; height: 4; radius: 2
                                color: window.p.control
                                Rectangle { width: dimSlider.visualPosition * parent.width; height: parent.height; radius: 2; color: window.accent }
                            }
                            handle: Rectangle {
                                x: dimSlider.leftPadding + dimSlider.visualPosition * (dimSlider.availableWidth - width)
                                y: dimSlider.topPadding + dimSlider.availableHeight / 2 - height / 2
                                width: 16; height: 16; radius: 8
                                color: dimSlider.pressed ? window.p.accentDown : window.accent
                                border.width: dimSlider.visualFocus ? 2 : 0
                                border.color: window.p.focus
                            }
                        }
                        Text {
                            text: appSettings.backgroundDim + "%"
                            color: window.muted
                            font.pixelSize: 12
                            Layout.preferredWidth: 34
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    SectionLabel { text: "WINDOW"; Layout.leftMargin: 20 }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 20; Layout.rightMargin: 20
                        spacing: 2
                        OptionSwitch {
                            objectName: "minimizeToTraySwitch"
                            text: "Minimize to tray"
                            detail: "Minimizing hides the window; click the tray icon to bring it back."
                            enabled: trayIcon.supported
                            checked: appSettings.minimizeToTray
                            onClicked: appSettings.minimizeToTray = !appSettings.minimizeToTray
                        }
                        OptionSwitch {
                            objectName: "closeToTraySwitch"
                            text: "Close to tray"
                            detail: "Closing keeps tds+ running in the tray. Exit from the tray menu or with End."
                            enabled: trayIcon.supported
                            checked: appSettings.closeToTray
                            onClicked: appSettings.closeToTray = !appSettings.closeToTray
                        }
                        OptionSwitch {
                            objectName: "startInTraySwitch"
                            text: "Start in tray"
                            detail: "Open hidden in the tray when tds+ starts."
                            enabled: trayIcon.supported
                            checked: appSettings.startInTray
                            onClicked: appSettings.startInTray = !appSettings.startInTray
                        }
                        OptionSwitch {
                            objectName: "alwaysOnTopSwitch"
                            text: "Always on top"
                            detail: "Keep the window above other windows, including Roblox."
                            checked: appSettings.alwaysOnTop
                            onClicked: appSettings.alwaysOnTop = !appSettings.alwaysOnTop
                        }
                        Text {
                            visible: !trayIcon.supported
                            text: "The tray is not available on this system."
                            color: window.dim
                            font.pixelSize: 11
                        }
                    }

                    Text {
                        visible: appSettings.lastError.length > 0
                        Layout.fillWidth: true
                        Layout.leftMargin: 20; Layout.rightMargin: 20
                        text: appSettings.lastError
                        color: window.p.danger
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 20; Layout.rightMargin: 20
                        Layout.topMargin: 6; Layout.bottomMargin: 16
                        Text {
                            Layout.fillWidth: true
                            text: "Saved automatically"
                            color: window.dim
                            font.pixelSize: 11
                        }
                        SoftButton {
                            objectName: "resetAppearanceButton"
                            text: "Reset look"
                            hint: "Graphite theme, waves, default accent"
                            onClicked: appSettings.resetAppearance()
                        }
                    }
                }
            }
        }
    }

    TitleBar {
        id: titleBar
        appWindow: window
        visible: window.customFrame
        anchors { top: parent.top; left: parent.left; right: parent.right }
        ink: window.ink
        muted: window.muted
        dim: window.dim
        animated: window.motionEnabled
        hoverColor: window.p.controlHover
        pressedColor: window.p.controlDown
        z: 20
    }

    // The system border is switched off for the frameless window, so draw a 1px one.
    Rectangle {
        anchors.fill: parent
        visible: window.customFrame && window.visibility !== Window.Maximized
        color: "transparent"
        border.width: 1
        border.color: window.borderColor
        z: 30
    }
}
