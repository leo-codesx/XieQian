import QtQuick

import ".."

/*!
    iOS 风格开关。
    开启：轨道亮绿 #34C759；关闭：轨道浅灰 #E5E5EA；滑块纯白带轻投影。
*/
Item {
    id: control

    width: 51
    height: 31

    property bool checked: false
    property bool interactive: true
    signal toggled(bool value)

    // 滑块位置与轨道颜色共用这个 0..1 进度，弹簧动效只挂在进度上
    property real progress: checked ? 1.0 : 0.0

    Behavior on progress {
        SpringAnimation {
            spring: 2.2
            damping: 0.25
        }
    }

    scale: mouse.pressed && interactive ? 0.94 : 1.0

    Behavior on scale {
        SpringAnimation {
            spring: 2.2
            damping: 0.25
        }
    }

    Rectangle {
        id: track

        anchors.fill: parent
        radius: height / 2
        color: Theme.mix(Theme.switchOff, Theme.switchOn, control.progress)
    }

    Rectangle {
        id: knobShadow

        y: 3
        x: 3 + control.progress * (control.width - width - 6)
        width: 27
        height: 27
        radius: width / 2
        color: "#1F000000"
    }

    Rectangle {
        id: knob

        y: 2
        x: 2 + control.progress * (control.width - width - 4)
        width: 27
        height: 27
        radius: width / 2
        color: Theme.white
    }

    MouseArea {
        id: mouse

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: control.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            if (control.interactive)
                control.toggled(!control.checked)
        }
    }
}
