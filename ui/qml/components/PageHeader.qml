import QtQuick

import ".."

/*!
    页面大标题：32px Bold 左对齐，可带副标题与右侧操作区。
    外层留白默认左右 16、上下 24；页面已有自己的边距时可传 0 覆盖。
*/
Item {
    id: root

    property string title: ""
    property string subtitle: ""
    property alias actions: actionSlot.data
    property int horizontalPadding: Theme.space4
    property int topPadding: Theme.space5
    property int bottomPadding: Theme.space5

    implicitHeight: titleColumn.implicitHeight + topPadding + bottomPadding
    height: implicitHeight

    Column {
        id: titleColumn

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: root.horizontalPadding
        anchors.rightMargin: root.horizontalPadding
        anchors.topMargin: root.topPadding
        spacing: Theme.space2

        Text {
            width: parent.width
            text: root.title
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontLargeTitle
            font.weight: Theme.weightBold
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            visible: root.subtitle.length > 0
            text: root.subtitle
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideRight
        }
    }

    Item {
        id: actionSlot

        anchors.right: parent.right
        anchors.rightMargin: root.horizontalPadding
        anchors.verticalCenter: titleColumn.verticalCenter
    }
}
