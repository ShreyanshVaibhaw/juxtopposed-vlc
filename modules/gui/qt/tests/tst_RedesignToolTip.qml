// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic as C
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedToolTip"
    width: 600; height: 400
    visible: true; when: windowShown
    RedesignButton { id: button; x: 20; y: 100; text: "Playback settings" }
    SignalSpy { id: closedSpy; target: findChild(button, "redesignToolTip"); signalName: "closed" }
    Window {
        id: offscreenWindow
        property var renderWindow: testCase.Window.window
        width: 300; height: 200
        visible: false
        RedesignButton { id: offscreenButton; text: "Audio" }
    }
    function init() {
        testCase.Window.window.visibility = Window.Windowed
        button.scaleFactor = 1; button.lightTheme = false
        button.text = "Playback settings"; button.tooltipTextFormat = Text.AutoText
        button.x = 20; button.y = 100
        const tip = findChild(button, "redesignToolTip")
        tip.close(); mouseMove(testCase, 400, 300)
    }
    function cleanup() {
        findChild(button, "redesignToolTip").close()
        testCase.Window.window.visibility = Window.Windowed
    }
    function test_fileNamesCanRequestPlainText() {
        const tip = findChild(button, "redesignToolTip")
        compare(tip.contentItem.textFormat, Text.AutoText)
        button.text = "/videos/<b>Favorite</b>/"
        button.tooltipTextFormat = Text.PlainText
        mouseMove(button, button.width / 2, button.height / 2)
        tryCompare(tip, "opened", true, 2000)
        compare(tip.contentItem.text, button.text)
        compare(tip.contentItem.textFormat, Text.PlainText)
    }
    function test_longFileNameFitsWindow_data() {
        return [{tag:"dark",scale:1,light:false},{tag:"light-scaled",scale:1.5,light:true}]
    }
    function test_longFileNameFitsWindow(data) {
        button.scaleFactor = data.scale; button.lightTheme = data.light
        button.text = "/videos/" + "Very_Long_File_Name_".repeat(20)
        button.tooltipTextFormat = Text.PlainText
        const tip = findChild(button, "redesignToolTip")
        mouseMove(button, button.width / 2, button.height / 2)
        tryCompare(tip, "opened", true, 2000)
        verify(tip.contentItem.lineCount > 1)
        const position = tip.contentItem.mapToItem(testCase, 0, 0)
        verify(position.x >= 0 && position.y >= 0)
        verify(position.x + tip.contentItem.width <= width)
        verify(position.y + tip.contentItem.height <= height)
    }
    function test_hoverUsesThemeAndApplicationFont() {
        const tip = findChild(button, "redesignToolTip")
        mouseMove(button, button.width / 2, button.height / 2)
        tryCompare(tip, "opened", true, 2000)
        compare(tip.text, button.text)
        compare(tip.background.color, button.theme.panel)
        compare(tip.contentItem.color, button.theme.bright)
        compare(tip.font.family, Qt.application.font.family)
        compare(tip.font.pixelSize, 12)
        compare(tip.popupType, C.Popup.Item)
        button.lightTheme = true; button.scaleFactor = 1.5
        compare(tip.background.color, button.theme.panel)
        compare(tip.font.pixelSize, 18)
    }
    function test_hoverStaysStableAtWindowEdges_data() {
        return [{tag: "title-left", x: 0, y: 0}, {tag: "title-right", x: 564, y: 0},
                {tag: "bottom-left", x: 0, y: 364}, {tag: "bottom-right", x: 564, y: 364}]
    }
    function test_hoverStaysStableAtWindowEdges(data) {
        const tip = findChild(button, "redesignToolTip")
        button.x = data.x; button.y = data.y
        mouseMove(button, button.width / 2, button.height / 2)
        tryCompare(tip, "opened", true, 2000)
        closedSpy.clear()
        wait(1200)
        verify(button.hovered)
        verify(tip.opened)
        compare(closedSpy.count, 0)
        compare(tip.popupType, C.Popup.Item)
        const position = tip.contentItem.mapToItem(testCase, 0, 0)
        verify(position.x >= 0 && position.y >= 0)
        verify(position.x + tip.contentItem.width <= width)
        verify(position.y + tip.contentItem.height <= height)
        mouseMove(testCase, 300, 250)
        tryCompare(tip, "opened", false)
    }
    function test_offscreenRenderingUsesAnItemPopup() {
        const tip = findChild(offscreenButton, "redesignToolTip")
        compare(tip.hostWindow, testCase.Window.window)
        compare(tip.popupType, C.Popup.Item)
    }
    function test_fullscreenUsesTheControlsWindow() {
        const tip = findChild(button, "redesignToolTip")
        testCase.Window.window.visibility = Window.FullScreen
        compare(tip.popupType, C.Popup.Item)
        mouseMove(button, button.width / 2, button.height / 2)
        tryCompare(tip, "opened", true, 2000)
        compare(tip.popupType, C.Popup.Item)
    }
}
