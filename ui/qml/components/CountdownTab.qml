import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import ".."

/*!
    高级倒计时：用单调时钟差值刷新，环形进度 + 预设 + 自定义时长。
*/
Item {
    id: root

    property int timerDurationMs: 60000
    property real timerRemainingMs: 60000
    property real timerEndMs: 0
    property bool timerRunning: false
    property bool timerFinished: false
    property bool timerCustomSyncing: false

    function timerFormatted(milliseconds) {
        var total = Math.max(0, Math.ceil(milliseconds / 1000))
        var hours = Math.floor(total / 3600)
        var minutes = Math.floor((total % 3600) / 60)
        var seconds = total % 60
        var pad = function(value) {
            return value < 10 ? "0" + value : String(value)
        }
        if (hours > 0)
            return hours + ":" + pad(minutes) + ":" + pad(seconds)
        return pad(minutes) + ":" + pad(seconds)
    }

    function timerStart() {
        if (timerRunning)
            return
        if (timerFinished || timerRemainingMs <= 0)
            timerRemainingMs = timerDurationMs
        timerFinished = false
        // 记录绝对结束时刻，后续只做「结束时刻 - 当前单调时刻」
        timerEndMs = backendBridge.monotonicMilliseconds() + timerRemainingMs
        timerRunning = true
    }

    function timerPause() {
        if (!timerRunning)
            return
        timerRemainingMs = Math.max(0, timerEndMs - backendBridge.monotonicMilliseconds())
        timerRunning = false
    }

    function timerReset() {
        timerRunning = false
        timerFinished = false
        timerRemainingMs = timerDurationMs
    }

    // 用单调时钟差值刷新，不靠 tick 累减；Tab 切走、窗口最小化后恢复都不会漂移
    function timerUpdate() {
        if (!timerRunning)
            return

        var remain = timerEndMs - backendBridge.monotonicMilliseconds()
        if (remain <= 0) {
            timerRemainingMs = 0
            timerRunning = false
            timerFinished = true
            if (Window.window && typeof Window.window.playWin === "function")
                Window.window.playWin()
            return
        }
        timerRemainingMs = remain
    }

    function timerApplyPreset(minutes) {
        timerDurationMs = minutes * 60000
        timerReset()
        timerFillCustom()
    }

    function timerFillCustom() {
        if (!hourField || !minuteField || !secondField)
            return
        timerCustomSyncing = true
        var total = Math.floor(timerDurationMs / 1000)
        hourField.text = String(Math.floor(total / 3600))
        minuteField.text = String(Math.floor((total % 3600) / 60))
        secondField.text = String(total % 60)
        timerCustomSyncing = false
    }

    function timerSyncCustom() {
        if (timerCustomSyncing)
            return
        if (!hourField || !minuteField || !secondField)
            return

        var hours = Math.max(0, parseInt(hourField.text) || 0)
        var minutes = Math.max(0, parseInt(minuteField.text) || 0)
        var seconds = Math.max(0, parseInt(secondField.text) || 0)
        var total = (hours * 3600 + minutes * 60 + seconds) * 1000
        if (total <= 0)
            return

        timerDurationMs = Math.min(24 * 3600 * 1000, total)
        timerReset()
    }

    // 100ms 只驱动刷新，剩余时间由单调时钟差值算出
    Timer {
        id: timerTick
        interval: 100
        repeat: true
        running: root.timerRunning
        onTriggered: root.timerUpdate()
    }

    Component.onCompleted: timerFillCustom()

    Flickable {
        id: timerFlick

        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: timerCard.height + 32
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {
            width: 0
            policy: ScrollBar.AlwaysOff
        }

        GroupedList {
            id: timerCard

            width: Math.min(560, timerFlick.width - 16)
            x: (timerFlick.width - width) / 2
            y: 16
            padding: 24
            spacing: 16

            Item {
                width: parent.width
                height: 260

                Canvas {
                    id: timerRing

                    anchors.centerIn: parent
                    width: Math.min(parent.width, parent.height, 260)
                    height: width
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()

                    Connections {
                        target: root

                        function onTimerRemainingMsChanged() {
                            timerRing.requestPaint()
                        }

                        function onTimerFinishedChanged() {
                            timerRing.requestPaint()
                        }

                        function onTimerDurationMsChanged() {
                            timerRing.requestPaint()
                        }
                    }

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)

                        var centerX = width / 2
                        var centerY = height / 2
                        var radius = Math.max(1, Math.min(centerX, centerY) - 18)
                        var thickness = Math.max(8, width * 0.045)

                        ctx.beginPath()
                        ctx.arc(centerX, centerY, radius, 0, Math.PI * 2)
                        ctx.lineWidth = thickness
                        ctx.strokeStyle = String(Theme.separator)
                        ctx.stroke()

                        var ratio = root.timerDurationMs > 0
                                    ? Math.max(0, Math.min(1,
                                        root.timerRemainingMs / root.timerDurationMs))
                                    : 0
                        if (ratio > 0) {
                            ctx.beginPath()
                            ctx.arc(centerX, centerY, radius, -Math.PI / 2,
                                    -Math.PI / 2 + Math.PI * 2 * ratio)
                            ctx.lineWidth = thickness
                            ctx.lineCap = "round"
                            ctx.strokeStyle = root.timerFinished
                                               ? String(Theme.success)
                                               : String(Theme.primary)
                            ctx.stroke()
                        }
                    }
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 4

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: root.timerFormatted(root.timerRemainingMs)
                        color: root.timerFinished ? Theme.success : Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Math.max(28, timerRing.width * 0.18)
                        font.weight: Font.Bold
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: root.timerFinished ? "时间到"
                              : (root.timerRunning ? "计时中"
                                 : (root.timerRemainingMs < root.timerDurationMs
                                    ? "已暂停" : "准备开始"))
                        color: root.timerFinished ? Theme.success : Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                    }
                }
            }

            Row {
                width: parent.width
                spacing: 8

                Repeater {
                    model: [1, 3, 5, 10, 25]

                    delegate: Rectangle {
                        width: (parent.width - 32) / 5
                        height: 40
                        radius: Theme.radiusControl
                        color: root.timerDurationMs === modelData * 60000
                               ? Theme.primarySoft : Theme.surface
                        border.width: 1
                        border.color: Theme.separator

                        Text {
                            anchors.centerIn: parent
                            text: modelData + " 分"
                            color: root.timerDurationMs === modelData * 60000
                                   ? Theme.primary : Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.timerApplyPreset(modelData)
                        }
                    }
                }
            }

            RowLayout {
                width: parent.width
                spacing: 8

                TextField {
                    id: hourField
                    Layout.preferredWidth: 84
                    Layout.preferredHeight: 40
                    horizontalAlignment: TextInput.AlignHCenter
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 0; top: 99 }
                    background: Rectangle {
                        radius: Theme.radiusControl
                        color: Theme.fill
                    }
                    onTextChanged: root.timerSyncCustom()
                }

                Text {
                    text: "时"
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                }

                TextField {
                    id: minuteField
                    Layout.preferredWidth: 84
                    Layout.preferredHeight: 40
                    horizontalAlignment: TextInput.AlignHCenter
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 0; top: 99 }
                    background: Rectangle {
                        radius: Theme.radiusControl
                        color: Theme.fill
                    }
                    onTextChanged: root.timerSyncCustom()
                }

                Text {
                    text: "分"
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                }

                TextField {
                    id: secondField
                    Layout.preferredWidth: 84
                    Layout.preferredHeight: 40
                    horizontalAlignment: TextInput.AlignHCenter
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 0; top: 99 }
                    background: Rectangle {
                        radius: Theme.radiusControl
                        color: Theme.fill
                    }
                    onTextChanged: root.timerSyncCustom()
                }

                Text {
                    text: "秒"
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                }

                Item {
                    Layout.fillWidth: true
                }
            }

            RowLayout {
                width: parent.width
                spacing: 12

                Item {
                    Layout.preferredWidth: 160
                    Layout.preferredHeight: Theme.buttonHeight

                    PrimaryButton {
                        width: parent.width
                        height: parent.height
                        text: root.timerRunning ? "暂停"
                              : (root.timerFinished ? "再来一次"
                                 : (root.timerRemainingMs < root.timerDurationMs
                                    ? "继续" : "开始"))
                        onClicked: {
                            if (root.timerRunning)
                                root.timerPause()
                            else
                                root.timerStart()
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.buttonHeight
                }

                Rectangle {
                    Layout.preferredWidth: 96
                    Layout.preferredHeight: 44
                    radius: Theme.radiusControl
                    color: resetMouse.containsMouse ? Theme.surfacePressed : Theme.surface
                    border.width: 1
                    border.color: Theme.separator

                    Text {
                        anchors.centerIn: parent
                        text: "重置"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                    }

                    MouseArea {
                        id: resetMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.timerReset()
                    }
                }
            }
        }
    }
}
