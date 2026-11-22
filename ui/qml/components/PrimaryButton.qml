import QtQuick
import QtQuick.Effects

import ".."

/*!
    主按钮：品牌渐变、按下缩放回弹，支持 loading（禁用 + 指示器）与 disabled 态。
*/
Item {
    id: button

    property string text: "保存并生成"
    property bool loading: false

    signal clicked()

    width: 208
    height: Theme.buttonHeight
    enabled: !loading
    opacity: enabled ? 1.0 : 0.45
    scale: mouse.pressed && enabled ? 0.96 : 1.0

    Behavior on opacity {
        NumberAnimation {
            duration: Theme.durationNormal
            easing.type: Easing.OutCubic
        }
    }

    Behavior on scale {
        SpringAnimation {
            spring: 2.2
            damping: 0.25
        }
    }

    Rectangle {
        id: bodyShadow

        anchors.fill: parent
        radius: Theme.radiusButton
        gradient: Theme.primaryGradient
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Theme.withAlpha(Theme.primary, 0.30)
            shadowBlur: 0.65
            shadowVerticalOffset: 7
            shadowHorizontalOffset: 0
            autoPaddingEnabled: true
        }
    }

    Rectangle {
        id: body

        anchors.fill: parent
        radius: Theme.radiusButton
        gradient: Theme.primaryGradient
        border.width: 1
        border.color: Theme.withAlpha(Theme.white, 0.18)
    }

    Rectangle {
        anchors.fill: body
        radius: body.radius
        color: Theme.white
        opacity: mouse.containsMouse && button.enabled ? 0.10 : 0.0

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.durationFast
                easing.type: Easing.OutCubic
            }
        }
    }

    Rectangle {
        anchors.fill: body
        radius: body.radius
        color: Theme.black
        opacity: mouse.pressed ? 0.06 : 0.0

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.durationFast
                easing.type: Easing.OutCubic
            }
        }
    }

    Canvas {
        id: spinner

        anchors.centerIn: parent
        width: 18
        height: 18
        visible: button.loading

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = Theme.white
            ctx.lineWidth = 2
            ctx.lineCap = "round"
            ctx.beginPath()
            ctx.arc(width / 2, height / 2, width / 2 - 2, 0, Math.PI * 1.4)
            ctx.stroke()
        }

        RotationAnimator on rotation {
            from: 0
            to: 360
            duration: 800
            loops: Animation.Infinite
            running: button.loading
        }
    }

    Text {
        anchors.centerIn: parent
        visible: !button.loading
        text: button.text
        color: Theme.white
        font.family: Theme.fontFamily
        font.pixelSize: 15
        font.weight: Font.DemiBold
        font.letterSpacing: 1.2
    }

    MouseArea {
        id: mouse

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: button.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            if (button.enabled)
                button.clicked()
        }
    }
}
