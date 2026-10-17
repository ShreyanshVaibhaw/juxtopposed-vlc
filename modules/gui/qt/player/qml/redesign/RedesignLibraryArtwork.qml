// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

Item {
    id: root
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property url source
    property string iconName: "video"
    property real iconSize: 24
    property real cornerRadius: 12
    property real progress: 0
    property bool showPlaceholderBackground: true
    property color surroundColor: theme.panel
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    clip: true
    Rectangle {
        anchors.fill: parent
        visible: root.showPlaceholderBackground || artworkImage.status === Image.Ready
        color: root.lightTheme ? root.theme.field : root.iconName === "folder" ? "#222222" : "#171717"
    }
    RedesignIcon { anchors.centerIn: parent; width: root.theme.dp(root.iconSize); height: width; name: root.iconName; color: root.theme.foreground; visible: artworkImage.status !== Image.Ready }
    Image { id: artworkImage; objectName: "libraryArtworkImage"; anchors.fill: parent; source: root.source; fillMode: Image.PreserveAspectCrop; asynchronous: true; visible: status === Image.Ready }
    Rectangle {
        anchors.left: parent.left; anchors.bottom: parent.bottom
        width: parent.width * Math.max(0, Math.min(1, root.progress))
        height: root.theme.dp(4); color: root.theme.accent
        visible: root.progress > 0
    }
    // A border masks the image corners on both native and software renderers.
    Rectangle {
        anchors.fill: parent; anchors.margins: -root.theme.dp(root.cornerRadius)
        radius: root.theme.dp(root.cornerRadius * 2)
        color: "transparent"; border.color: root.surroundColor
        border.width: root.theme.dp(root.cornerRadius)
        visible: root.cornerRadius > 0 && (root.showPlaceholderBackground || artworkImage.status === Image.Ready)
    }
}
