import QtQuick
import QtQuick.Layouts

import ".."

/*!
    抽奖规则对话框：抽取人数（1-10）与策略选择。

    由 HomePage 顶部的规则按钮挂载，写入 backendBridge.updateDrawSettings，
    设置会随 Config 表持久化，下次启动仍然生效。
*/
Item {
    id: dialogRoot

    property bool opened: false
    property int count: 1
    property int selected: 0
    property bool dynamicHint: false

    anchors.fill: parent
    visible: opacity > 0.001
    opacity: opened ? 1.0 : 0.0
    z: 200
    focus: opened

    Keys.onEscapePressed: function(event) {
        dialogRoot.close()
        event.accepted = true
    }

    readonly property var strategies: [
        { "label": "等概率抽取", "desc": "所有人机会相同" },
        { "label": "防连抽", "desc": "优先未中签的人，全部抽过后再回到已中签者" },
        { "label": "加权抽取", "desc": "按名单权重分配概率，权重越高越容易被抽中" }
    ]

    function open() {
        count = Math.max(1, Math.min(10, backendBridge.drawCount))
        selected = Math.max(0, Math.min(2, backendBridge.drawStrategy))
        dynamicHint = backendBridge.dynamicWeights
        opened = true
    }

    function close() {
        opened = false
    }

    function apply() {
        backendBridge.updateDrawSettings(count, selected)
        close()
    }

    Behavior on opacity {
        NumberAnimation {
            duration: Theme.durationFast
            easing.type: Easing.OutCubic
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.overlay

        MouseArea {
            anchors.fill: parent
            onClicked: dialogRoot.close()
        }
    }

    Rectangle {
        id: card

        anchors.centerIn: parent
        width: Math.min(460, dialogRoot.width - 64)
        height: body.implicitHeight + 56
        radius: Theme.radiusDialog
        color: Theme.surface
        scale: dialogRoot.opened ? 1.0 : 0.96

        Behavior on scale {
            NumberAnimation {
                duration: Theme.durationNormal
                easing.type: Easing.OutCubic
            }
        }

        Column {
            id: body

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 26
            anchors.rightMargin: 26
            spacing: 16

            Text {
                text: "抽奖规则"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontH1
                font.weight: Font.Bold
            }

            // ── 抽取人数 ──
            Rectangle {
                width: parent.width
                height: 64
                radius: Theme.radiusControl
                color: Theme.surfaceAlt

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: "抽取人数"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    StepButton {
                        text: "−"
                        enabled: dialogRoot.count > 1
                        onClicked: dialogRoot.count = Math.max(1, dialogRoot.count - 1)
                    }

                    Text {
                        width: 44
                        height: 36
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: dialogRoot.count
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }

                    StepButton {
                        text: "+"
                        enabled: dialogRoot.count < 10
                        onClicked: dialogRoot.count = Math.min(10, dialogRoot.count + 1)
                    }
                }
            }

            // ── 策略 ──
            Column {
                width: parent.width
                spacing: 8

                Repeater {
                    model: dialogRoot.strategies

                    delegate: Rectangle {
                        id: strategyRow

                        width: body.width
                        height: 62
                        radius: Theme.radiusControl
                        color: dialogRoot.selected === index
                               ? Theme.primarySoft
                               : Theme.surfaceAlt
                        border.width: 1
                        border.color: dialogRoot.selected === index
                                      ? Theme.primary
                                      : Theme.border

                        Rectangle {
                            id: radio

                            anchors.left: parent.left
                            anchors.leftMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            width: 20
                            height: 20
                            radius: 10
                            color: dialogRoot.selected === index ? Theme.primary : Theme.surface
                            border.width: 1
                            border.color: dialogRoot.selected === index
                                          ? Theme.primary
                                          : Theme.borderStrong

                            Rectangle {
                                anchors.centerIn: parent
                                width: 8
                                height: 8
                                radius: 4
                                color: Theme.white
                                visible: dialogRoot.selected === index
                            }
                        }

                        Column {
                            anchors.left: radio.right
                            anchors.leftMargin: 12
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Text {
                                width: parent.width
                                text: modelData.label
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                font.weight: Font.DemiBold
                            }

                            Text {
                                width: parent.width
                                text: modelData.desc
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMicro
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: dialogRoot.selected = index
                        }
                    }
                }
            }

            Text {
                width: parent.width
                visible: dialogRoot.dynamicHint
                text: "动态权重已开启：抽中次数越多，下一次权重衰减越大。"
                color: Theme.warning
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            Row {
                width: parent.width
                spacing: 10

                Item { width: Math.max(0, parent.width - 238); height: 1 }

                DialogAction {
                    width: 96
                    height: 42
                    text: "取消"
                    onClicked: dialogRoot.close()
                }

                DialogAction {
                    width: 112
                    height: 42
                    text: "应用"
                    primary: true
                    onClicked: dialogRoot.apply()
                }
            }
        }
    }

    component StepButton: Item {
        id: step

        property string text: "+"
        signal clicked()

        width: 36
        height: 36
        opacity: enabled ? 1.0 : 0.35

        Rectangle {
            anchors.fill: parent
            radius: 10
            color: mouse.containsMouse && step.enabled ? Theme.surface : Theme.surfaceAlt
            border.width: 1
            border.color: Theme.border
        }

        Text {
            anchors.centerIn: parent
            text: step.text
            color: Theme.primary
            font.family: Theme.fontFamily
            font.pixelSize: 20
            font.weight: Font.DemiBold
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: step.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: {
                if (step.enabled)
                    step.clicked()
            }
        }
    }

    component DialogAction: Item {
        id: action

        property string text: ""
        property bool primary: false
        signal clicked()

        height: 42
        scale: mouse.pressed ? 0.96 : 1.0

        Behavior on scale {
            NumberAnimation {
                duration: Theme.durationFast
                easing.type: Easing.OutCubic
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: action.primary ? Theme.primary : Theme.surface
            border.width: action.primary ? 0 : 1
            border.color: Theme.border
        }

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: action.primary ? Theme.white : Theme.primarySoft
            opacity: mouse.containsMouse ? (action.primary ? 0.12 : 0.55) : 0.0

            Behavior on opacity {
                NumberAnimation {
                    duration: Theme.durationFast
                    easing.type: Easing.OutCubic
                }
            }
        }

        Text {
            anchors.centerIn: parent
            text: action.text
            color: action.primary ? Theme.white : Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            font.weight: Font.DemiBold
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: action.clicked()
        }
    }
}
