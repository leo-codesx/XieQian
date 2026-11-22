import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import ".."

/*!
    请求诊断弹窗：人话文案 + 脱敏技术详情（AiManager.lastDiagnostic）。
    由上层在 diagnosisRequested 时调用 openWith(message)。
*/
Item {
    id: root

    anchors.fill: parent
    visible: false
    z: 1000

    property string message: ""

    function openWith(text) {
        root.message = (text && String(text).length > 0) ? String(text) : "AI 请求没有完成"
        root.visible = true
    }

    function closeDialog() {
        root.visible = false
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.scrim

        MouseArea {
            anchors.fill: parent
            onClicked: root.closeDialog()
        }
    }

    Rectangle {
        id: card

        width: Math.min(560, parent.width - 80)
        height: Math.min(520, parent.height - 80)
        anchors.centerIn: parent
        radius: Theme.radiusCard
        color: Theme.surface

        CardShadow {}

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.space5
            spacing: Theme.space3

            Text {
                Layout.fillWidth: true
                text: "请求诊断"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontH1
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: root.message
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.radiusControl
                color: Theme.fill

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: Theme.space2
                    clip: true

                    TextArea {
                        id: diagnosticArea

                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextArea.WrapAnywhere
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                        background: null
                        text: {
                            var manager = backendBridge.aiManager
                            var detail = manager ? String(manager.lastDiagnostic) : ""
                            return detail.length > 0 ? detail : "暂无诊断信息"
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.buttonHeight
                spacing: Theme.space3

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                Item {
                    Layout.preferredWidth: 150
                    Layout.fillHeight: true

                    PrimaryButton {
                        width: parent.width
                        height: parent.height
                        text: "复制诊断信息"
                        onClicked: {
                            diagnosticArea.selectAll()
                            diagnosticArea.copy()
                            diagnosticArea.deselect()
                        }
                    }
                }

                Item {
                    Layout.preferredWidth: 72
                    Layout.fillHeight: true

                    Text {
                        anchors.centerIn: parent
                        text: "关闭"
                        color: Theme.primary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.closeDialog()
                    }
                }
            }
        }
    }
}
