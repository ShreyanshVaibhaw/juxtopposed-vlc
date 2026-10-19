// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedIconSizing"
    width: 400; height: 160
    visible: true; when: windowShown

    Component {
        id: iconComponent
        RedesignIcon { name: "folder" }
    }
    Component {
        id: rowComponent
        RowLayout {
            property alias glyph: glyph
            width: 280; height: 44
            RedesignIcon {
                id: glyph
                name: "folder"
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
            }
            Text { Layout.fillWidth: true; text: "Queue item" }
        }
    }
    function init() {
        if (typeof failOnWarning === "function") failOnWarning(/Binding loop detected/)
    }
    function test_intrinsicSizeDoesNotFollowRasterRequests() {
        const icon = createTemporaryObject(iconComponent, testCase)
        verify(icon !== null)
        tryCompare(icon, "status", Image.Ready)
        compare(icon.width, 24); compare(icon.height, 24)
        for (const size of [18, 30, 42.5, 16]) {
            icon.width = size; icon.height = size
            wait(10)
            compare(icon.implicitWidth, 24)
            compare(icon.implicitHeight, 24)
            compare(icon.sourceSize.width, Math.round(size * Screen.devicePixelRatio))
            compare(icon.sourceSize.height, Math.round(size * Screen.devicePixelRatio))
        }
    }
    function test_layoutSizeIsStable_data() {
        return [
            {tag: "visible", shown: true},
            {tag: "hidden", shown: false}
        ]
    }
    function test_layoutSizeIsStable(data) {
        const row = createTemporaryObject(rowComponent, testCase)
        verify(row !== null)
        const icon = row.glyph
        icon.visible = data.shown
        for (const size of [18, 24, 36, 15.3]) {
            icon.Layout.preferredWidth = size
            icon.Layout.preferredHeight = size
            row.width = 300 + size
            row.height = 44 + size
            wait(15)
            compare(icon.implicitWidth, 24)
            compare(icon.implicitHeight, 24)
            if (data.shown) {
                fuzzyCompare(icon.width, size, 1)
                fuzzyCompare(icon.height, size, 1)
            }
            icon.visible = !icon.visible
            icon.name = icon.name === "folder" ? "playlist" : "folder"
            icon.color = icon.color === "#8f8f8f" ? "#228e53" : "#8f8f8f"
            wait(15)
            compare(icon.implicitWidth, 24)
            compare(icon.implicitHeight, 24)
            icon.visible = data.shown
        }
    }
}
