// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    name: "JuxtopposedPlugins"
    width: 900; height: 500; visible: true; when: windowShown
    RedesignPlugins { id: page; width: 784 }
    function item(name) { return findChild(page, name) }
    function init() {
        page.width = 784; page.lightTheme = false
        page.sortColumn = "name"; page.sortDescending = false
        page.plugins = [
            {name: "Zebra video", type: "video decoder", priority: 2, identifier: "avcodec"},
            {name: "Alpha audio", type: "audio output", priority: 10, identifier: "alsa"},
            {name: "Beta log", type: "logger", priority: -5, identifier: "file logger"}]
        item("pluginsSearch").clear(); wait(30)
    }
    function test_defaultSortDoesNotChangeTheSourceCatalog() {
        compare(page.visiblePlugins.map(plugin => plugin.name).join(","), "Alpha audio,Beta log,Zebra video")
        compare(page.plugins[0].name, "Zebra video")
        compare(item("pluginsList").count, 3)
    }
    function test_prioritySortUsesNumbersAndChangesDirection() {
        mouseClick(item("pluginsPriorityHeader")); wait(20)
        compare(page.visiblePlugins.map(plugin => plugin.priority).join(","), "-5,2,10")
        compare(findChild(item("pluginRow_0"), "pluginPriority").text, "-5")
        mouseClick(item("pluginsPriorityHeader")); wait(20)
        compare(page.visiblePlugins.map(plugin => plugin.priority).join(","), "10,2,-5")
    }
    function test_searchMatchesThePluginIdentifierAndType() {
        const search = item("pluginsSearch")
        search.text = "AVCODEC"; wait(20)
        compare(page.visiblePlugins.length, 1); compare(page.visiblePlugins[0].name, "Zebra video")
        search.text = "AUDIO OUTPUT"; wait(20)
        compare(page.visiblePlugins.length, 1); compare(page.visiblePlugins[0].identifier, "alsa")
        search.text = "nothing matches"; wait(20)
        compare(item("pluginsList").count, 0)
    }
    function test_keyboardSelectsWithoutChangingMetadata() {
        const list = item("pluginsList"); list.forceActiveFocus()
        keyClick(Qt.Key_End); compare(list.currentIndex, 2)
        keyClick(Qt.Key_Home); compare(list.currentIndex, 0)
        compare(page.plugins[0].priority, 2)
        item("pluginsSearch").text = "logger"; wait(20)
        compare(list.currentIndex, -1)
    }
    function test_narrowPageKeepsColumnsAndSearchInsideTheCard() {
        page.width = 244; wait(30)
        for (const name of ["pluginsNameHeader", "pluginsTypeHeader", "pluginsPriorityHeader", "pluginsSearch", "pluginsCount"]) {
            const control = item(name), point = control.mapToItem(page, 0, 0)
            verify(point.x >= 0, name)
            verify(point.x + control.width <= page.width + 1, name + ": " + point.x + " + " + control.width + " > " + page.width)
        }
        const row = item("pluginRow_0")
        for (const name of ["pluginName", "pluginType", "pluginPriority"]) {
            const cell = findChild(row, name), point = cell.mapToItem(page, 0, 0)
            verify(point.x >= 0, name); verify(point.x + cell.width <= page.width + 1, name)
        }
    }
    function test_pluginMetadataIsPlainTextInBothThemes() {
        page.plugins = [{name: "<Plugin>", type: "<Type>", priority: 10, identifier: "<Identifier>"}]
        for (const light of [false, true]) {
            page.lightTheme = light; wait(20)
            const row = item("pluginRow_0")
            compare(findChild(row, "pluginName").text, "<Plugin>")
            compare(findChild(row, "pluginName").textFormat, Text.PlainText)
            compare(findChild(row, "redesignToolTip").contentTextFormat, Text.PlainText)
        }
    }
}
