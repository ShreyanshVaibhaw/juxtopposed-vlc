// SPDX-License-Identifier: GPL-2.0-or-later
// An explicit, styled tooltip. The shared attached C.ToolTip instance is unreliable to style
// (Qt 6.11 can hand out a different instance than the one configured), so redesign controls
// declare their own.
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic as C

C.ToolTip {
    id: tip
    objectName: "redesignToolTip"
    property RedesignTheme theme
    property int contentTextFormat: Text.AutoText
    readonly property var controlWindow: parent ? parent.Window.window : null
    readonly property var hostWindow: controlWindow && controlWindow.renderWindow !== undefined
                                      ? controlWindow.renderWindow : controlWindow
    readonly property real s: theme ? theme.scaleFactor : 1

    // A native tooltip window can make Windows send hover-leave to its owner.
    // Hiding it then restores hover and opens it again, including on title-bar
    // buttons. Keep every tooltip in the controls' overlay so it cannot change
    // native window hover/focus. Popup margins keep edge tooltips on screen.
    popupType: C.Popup.Item
    delay: 650
    margins: 6 * s
    width: Math.min(implicitWidth, 360 * s,
                    hostWindow ? Math.max(0, hostWindow.width - 2 * margins) : 360 * s)
    x: parent ? (parent.width - width) / 2 : 0
    padding: 6 * s
    leftPadding: 8 * s
    rightPadding: 8 * s
    font.family: Qt.application.font.family
    font.pixelSize: 12 * s

    contentItem: Text {
        text: tip.text
        textFormat: tip.contentTextFormat
        font: tip.font
        color: tip.theme ? tip.theme.bright : "#e5e5e5"
        wrapMode: Text.Wrap
    }
    background: Rectangle {
        radius: 6 * tip.s
        color: tip.theme ? tip.theme.panel : "#101010"
        border.width: 1
        border.color: tip.theme ? tip.theme.border : "#292929"
    }
}
