import QtQuick
import QtQuick.Layouts

import ".."

/*!
    函数表达式数学键盘。
    只负责往 target（TextArea/TextField）里插入文本与整体退格，不做表达式校验。
    variableKeys 由页面按当前模式给出：直角坐标 x、参数方程 t、隐式方程 x+y。
*/
Item {
    id: root

    property var target: null
    property var variableKeys: ["x"]
    property bool collapsed: false

    width: parent ? parent.width : 0
    implicitHeight: collapsed ? header.height : header.height + Theme.space2 + body.implicitHeight
    height: implicitHeight

    readonly property var keys: {
        var list = ["sin(", "cos(", "tan(", "ln(", "log(", "abs(", "sqrt("]
        for (var i = 0; i < variableKeys.length; ++i)
            list.push(String(variableKeys[i]))
        list.push("pi")
        list.push("e")
        list.push("7"); list.push("8"); list.push("9")
        list.push("4"); list.push("5"); list.push("6")
        list.push("1"); list.push("2"); list.push("3")
        list.push("0"); list.push("."); list.push(",")
        list.push("+"); list.push("-"); list.push("*")
        list.push("/"); list.push("^"); list.push("(")
        list.push(")"); list.push("=")
        return list
    }

    function insertText(text) {
        if (!target)
            return

        var position = target.cursorPosition
        if (position < 0 || position > target.length)
            position = target.length

        target.insert(position, text)
        target.cursorPosition = position + text.length
        target.forceActiveFocus()
    }

    // 函数名与常量整体删除，其余字符逐个删除
    function backspace() {
        if (!target)
            return

        var position = target.cursorPosition
        if (position <= 0)
            return

        var content = String(target.text)
        var units = ["sin(", "cos(", "tan(", "ln(", "log(", "abs(", "sqrt(", "pi"]
        var start = position - 1

        for (var i = 0; i < units.length; ++i) {
            var unit = units[i]
            if (position >= unit.length
                    && content.substring(position - unit.length, position) === unit) {
                start = position - unit.length
                break
            }
        }

        // e 作为常量整体删除，但只在它独立出现时（避免切掉变量名或数字的一部分）
        if (start === position - 1 && content.charAt(position - 1) === "e") {
            var previous = position >= 2 ? content.charAt(position - 2) : ""
            if (!/[A-Za-z0-9_.]/.test(previous))
                start = position - 1
        }

        target.remove(start, position)
        target.cursorPosition = start
    }

    function handleKey(key) {
        if (key === "\u232b")
            backspace()
        else
            insertText(key)
    }

    Item {
        id: header

        width: root.width
        height: 24

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "数学键盘"
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
        }

        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: root.collapsed ? "展开" : "收起"
            color: Theme.primary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption

            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.collapsed = !root.collapsed
            }
        }
    }

    GridLayout {
        id: body

        anchors.top: header.bottom
        anchors.topMargin: Theme.space2
        anchors.left: parent.left
        anchors.right: parent.right
        visible: !root.collapsed
        columns: 5
        columnSpacing: Theme.space1
        rowSpacing: Theme.space1

        Repeater {
            model: root.keys

            delegate: Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                radius: Theme.radiusControl
                color: keyMouse.containsMouse ? Theme.surfacePressed : Theme.surface
                border.width: 1
                border.color: Theme.separator

                Text {
                    anchors.centerIn: parent
                    text: modelData
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.DemiBold
                }

                MouseArea {
                    id: keyMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.handleKey(modelData)
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            radius: Theme.radiusControl
            color: backMouse.containsMouse ? Theme.surfacePressed : Theme.surface
            border.width: 1
            border.color: Theme.separator

            Text {
                anchors.centerIn: parent
                text: "\u232b"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
            }

            MouseArea {
                id: backMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.backspace()
            }
        }
    }
}
