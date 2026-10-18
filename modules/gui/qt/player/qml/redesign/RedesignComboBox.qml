// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts

C.ComboBox {
    id: root
    property real scaleFactor: 1
    property bool lightTheme: false
    property bool showCurvePreviews: false
    property bool showSelectionMark: showCurvePreviews
    // Opt in for array models whose unavailable rows have enabled: false.
    property bool restrictChoices: false
    property real choiceHeight: 34
    property real popupGap: 4
    property var labelOverrides: ({})
    function labelAt(index) { const text = textAt(index); return labelOverrides[text] || text }
    function choiceEnabled(index) {
        return index >= 0 && index < count && (!restrictChoices || model[index].enabled !== false)
    }
    function nextChoice(index, direction) {
        for (let i = index + direction; i >= 0 && i < count; i += direction)
            if (choiceEnabled(i)) return i
        return -1
    }
    function navigateTo(index) {
        if (!choiceEnabled(index)) return
        if (!popup.visible) {
            if (currentIndex !== index) { currentIndex = index; activated(index) }
            return
        }
        // The template's highlightedIndex is read-only. Its public navigation
        // methods update it without activating intermediate popup rows.
        while (highlightedIndex < index) incrementCurrentIndex()
        while (highlightedIndex > index) decrementCurrentIndex()
    }
    Keys.priority: Keys.BeforeItem
    Keys.onPressed: event => {
        if (!restrictChoices || editable) return
        const index = popup.visible ? highlightedIndex : currentIndex
        let target = -1
        if (event.key === Qt.Key_Down) target = nextChoice(index, 1)
        else if (event.key === Qt.Key_Up) target = nextChoice(index < 0 ? count : index, -1)
        else if (event.key === Qt.Key_Home) target = nextChoice(-1, 1)
        else if (event.key === Qt.Key_End) target = nextChoice(count, -1)
        else if (event.text.length > 0 && event.key !== Qt.Key_Space
                 && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))) {
            for (let offset = 1; offset <= count; ++offset) {
                const row = (index + offset + count) % count
                if (choiceEnabled(row) && labelAt(row).toLocaleLowerCase().startsWith(event.text.toLocaleLowerCase())) {
                    target = row
                    break
                }
            }
        } else return
        navigateTo(target)
        event.accepted = true
    }
    signal aboutToOpen()
    property color accentColor: "#d37918"
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    implicitHeight: theme.dp(34)
    font.pixelSize: theme.dp(14)
    leftPadding: theme.dp(14); rightPadding: theme.dp(40)
    contentItem: Text {
        text: root.displayText; font: root.font
        color: root.theme.foreground
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
    background: Rectangle {
        radius: root.theme.dp(4); color: root.theme.background
        border.color: root.visualFocus ? root.theme.accent : root.theme.field
    }
    indicator: RedesignIcon {
        name: "chevron-down"; color: root.theme.foreground
        width: root.theme.dp(20); height: width
        x: root.width - width - root.theme.dp(14); y: (root.height - height) / 2
    }
    delegate: C.ItemDelegate {
        id: choiceDelegate
        required property int index
        objectName: "comboChoice_" + index
        enabled: root.choiceEnabled(index)
        width: ListView.view.width
        implicitHeight: root.theme.dp(root.theme.compactMenus ? Math.min(28, root.choiceHeight) : root.choiceHeight)
        leftPadding: root.theme.compactMenus ? root.theme.dp(8) : padding; rightPadding: leftPadding
        topPadding: root.theme.compactMenus ? 0 : padding; bottomPadding: topPadding
        text: root.labelAt(index)
        highlighted: root.highlightedIndex === index
        contentItem: RowLayout {
            spacing: root.theme.dp(10)
            RedesignSvg {
                readonly property var curve: root.model && root.model[index] ? root.model[index].curve || [] : []
                readonly property color fillColor: root.theme.accent
                readonly property color lineColor: root.theme.foreground
                Layout.preferredWidth: root.theme.dp(24); Layout.preferredHeight: root.theme.dp(18)
                visible: !root.theme.compactMenus && root.showCurvePreviews && curve.length > 0
                sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                svg: {
                    if (curve.length < 2 || width <= 0 || height <= 0) return ""
                    const middle = height / 2
                    let path = "M0 " + middle
                    for (let i = 0; i < curve.length; ++i)
                        path += " L" + (i * width / (curve.length - 1)) + " " + (middle - Number(curve[i]) * middle / 20)
                    path += " L" + width + " " + middle + " Z"
                    return (
                        '<svg xmlns="http://www.w3.org/2000/svg" width="' + width + '" height="' + height + '" viewBox="0 0 ' + width + ' ' + height + '">'
                        + '<path d="' + path + '" fill="' + fillColor + '"/>'
                        + '<path d="M0 ' + middle + ' H' + width + '" fill="none" stroke="' + lineColor + '" stroke-width="1"/></svg>')
                }
            }
            Text {
                Layout.fillWidth: true
                text: root.labelAt(index); font: root.font; color: root.theme.foreground
                opacity: choiceDelegate.enabled ? 1 : .4
                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
            }
            Item {
                Layout.preferredWidth: root.theme.dp(20); Layout.preferredHeight: root.theme.dp(20)
                visible: root.showSelectionMark && index === root.currentIndex
                RedesignIcon { anchors.fill: parent; name: "check"; color: root.theme.accent }
            }
        }
        background: Rectangle { radius: root.theme.dp(3); color: parent.highlighted ? root.theme.hover : "transparent" }
    }
    popup: C.Popup {
        onAboutToShow: root.aboutToOpen()
        y: root.height + root.theme.dp(root.popupGap)
        width: root.width
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, root.theme.dp(300))
        padding: root.theme.menuPadding
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            C.ScrollIndicator.vertical: C.ScrollIndicator { }
        }
        background: Rectangle { color: root.theme.panel; radius: root.theme.dp(6); border.color: root.theme.border }
    }
}
