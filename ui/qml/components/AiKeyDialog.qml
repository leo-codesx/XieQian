import QtQuick
import QtQuick.Window

import ".."

/*!
    AI 未配置密钥时的统一引导弹窗。

    零信任模型下应用内不存在任何内置密钥，所有 AI 入口（OCR、成绩调权、
    颁奖词、函数生成）在缺少用户配置时都必须先弹出本对话框，禁止发起后台请求。
*/
Item {
    id: dialog

    property bool opened: false
    // 默认直接跳设置页；结果贺卡等覆盖层场景可关闭该行为并自行处理
    property bool navigateToSettings: true

    anchors.fill: parent
    visible: opacity > 0.001
    opacity: opened ? 1.0 : 0.0
    z: 900
    focus: opened

    Keys.onEscapePressed: function(event) {
        dialog.close()
        event.accepted = true
    }

    signal configureRequested()

    function open() {
        opened = true
    }

    function close() {
        opened = false
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
            onClicked: dialog.close()
        }
    }

    Rectangle {
        id: card

        anchors.centerIn: parent
        width: Math.min(420, dialog.width - 64)
        height: contentColumn.height + 48
        radius: Theme.radiusDialog
        color: Theme.surface
        scale: dialog.opened ? 1.0 : 0.96

        Behavior on scale {
            NumberAnimation {
                duration: Theme.durationNormal
                easing.type: Easing.OutCubic
            }
        }

        Column {
            id: contentColumn

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 28
            anchors.rightMargin: 28
            spacing: 14

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: "需要先配置 API Key"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontH2
                font.weight: Font.DemiBold
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: "AI 图片识别、成绩调权、颁奖词与函数生成都使用你在设置页填写的 Key。"
                      + "应用不内置任何密钥；未配置时不会发起任何请求。"
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.WordWrap
                lineHeight: 1.35
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 10

                AiDialogButton {
                    width: 104
                    height: 40
                    text: "稍后"
                    onClicked: dialog.close()
                }

                AiDialogButton {
                    width: 132
                    height: 40
                    text: "前往设置"
                    primary: true
                    onClicked: {
                        dialog.close()
                        if (dialog.navigateToSettings) {
                            if (Window.window && typeof Window.window.navigate === "function")
                                Window.window.navigate(6)
                        } else {
                            dialog.configureRequested()
                        }
                    }
                }
            }
        }
    }

    component AiDialogButton: Item {
        id: button

        property string text: ""
        property bool primary: false
        signal clicked()

        width: 110
        height: 40
        scale: mouse.pressed ? 0.95 : 1.0

        Behavior on scale {
            NumberAnimation {
                duration: Theme.durationFast
                easing.type: Easing.OutCubic
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: button.primary ? Theme.primary : Theme.surface
            border.width: button.primary ? 0 : 1
            border.color: Theme.border
        }

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: button.primary ? Theme.white : Theme.primarySoft
            opacity: mouse.containsMouse ? (button.primary ? 0.12 : 0.6) : 0.0

            Behavior on opacity {
                NumberAnimation {
                    duration: Theme.durationFast
                    easing.type: Easing.OutCubic
                }
            }
        }

        Text {
            anchors.centerIn: parent
            text: button.text
            color: button.primary ? Theme.white : Theme.primary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
            font.weight: Font.DemiBold
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: button.clicked()
        }
    }
}
