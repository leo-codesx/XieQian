import QtQuick

import ".."

/*!
    分组卡片：纯白圆角外壳，行按声明顺序垂直排列。
    行内分隔线由 ListRow 负责，卡片本身不画线。
*/
Item {
    id: root

    property int cornerRadius: Theme.radiusCard
    property int padding: 0
    property alias spacing: contentColumn.spacing
    default property alias contentData: contentColumn.data

    width: parent ? parent.width : 0
    implicitHeight: contentColumn.implicitHeight + padding * 2
    height: implicitHeight

    Rectangle {
        anchors.fill: parent
        radius: root.cornerRadius
        color: Theme.surface

        CardShadow {}
    }

    Column {
        id: contentColumn
        x: root.padding
        y: root.padding
        width: parent.width - root.padding * 2
    }
}
