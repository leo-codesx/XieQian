import QtQuick

import ".."

/*!
    分组卡片内的一行。
    顶部发丝分割线左缩 16、右贴边，首行不画；pressable 时提供整行点击与按压底色。
*/
Item {
    id: root

    property bool first: false
    property bool showSeparator: true
    property bool pressable: false

    signal clicked()

    width: parent ? parent.width : 0
    implicitHeight: Theme.rowHeight
    default property alias contentData: body.data

    Rectangle {
        anchors.fill: parent
        color: Theme.surfacePressed
        opacity: root.pressable && pressArea.pressed ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.durationFast
                easing.type: Easing.OutCubic
            }
        }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.space4
        height: root.first || !root.showSeparator ? 0 : 1
        color: Theme.separator
    }

    MouseArea {
        id: pressArea

        anchors.fill: parent
        enabled: root.pressable
        hoverEnabled: root.pressable
        cursorShape: root.pressable ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.clicked()
    }

    Item {
        id: body

        anchors.fill: parent
    }
}
