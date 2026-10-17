// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedNetwork"
    width: 1280; height: 800
    visible: true; when: windowShown
    RedesignNetworkDialog { id: dialog; parent: testCase }
    SignalSpy { id: saveSpy; target: dialog; signalName: "saveRequested" }
    SignalSpy { id: acceptedSpy; target: dialog; signalName: "accepted" }
    function init() {
        width = 1280; height = 800
        dialog.scaleFactor = 1; dialog.lightTheme = false
        dialog.busy = false; dialog.canSavePlaylist = true; dialog.close()
        saveSpy.clear(); acceptedSpy.clear()
        dialog.begin(); tryCompare(dialog, "opened", true)
    }
    function cleanup() { dialog.busy = false; dialog.close(); tryCompare(dialog, "visible", false) }
    function test_emptyAndInvalidLinksDoNotSave() {
        const save = findChild(dialog, "networkSaveButton")
        verify(!save.enabled)
        for (const text of [" ", "example.com", "https://one.test\nwrong", "https://bad link.test"]) {
            dialog.urlsText = text
            verify(!save.enabled, text)
            dialog.submit()
        }
        compare(saveSpy.count, 0)
    }
    function test_referenceFieldGeometry() {
        compare(dialog.width, 1048); compare(dialog.height, 631)
        for (const spec of [
            {name: "networkUrlsField", x: 20, y: 150, width: 1008, height: 194},
            {name: "networkPlaylistNameField", x: 20, y: 464, width: 1008, height: 37},
            {name: "networkCancelButton", x: 20, y: 579, width: 100, height: 32},
            {name: "networkSaveButton", x: 928, y: 579, width: 100, height: 32}
        ]) {
            const item = findChild(dialog, spec.name)
            const point = item.mapToItem(dialog.background, 0, 0)
            fuzzyCompare(point.x, spec.x, 0.1); fuzzyCompare(point.y, spec.y, 0.1)
            compare(item.width, spec.width); compare(item.height, spec.height)
        }
    }
    function test_widthChangeKeepsFocusedNameVisible() {
        height = 400
        const name = findChild(dialog, "networkPlaylistNameField")
        name.forceActiveFocus(); keyClick(Qt.Key_A)
        verify(waitForRendering(name))
        function revealed() {
            const point = name.mapToItem(dialog.background, 0, 0)
            return name.activeFocus && point.y >= 50 && point.y + name.height <= dialog.height - 72
        }
        tryVerify(revealed)
        width = 260
        tryVerify(revealed)
        keyClick(Qt.Key_B)
        compare(dialog.playlistName, "ab")
    }
    function test_smallWindowRevealsTheFocusedField() {
        width = 640; height = 400
        wait(20)
        const name = findChild(dialog, "networkPlaylistNameField")
        name.forceActiveFocus(); keyClick(Qt.Key_A)
        compare(dialog.playlistName, "a")
        tryVerify(() => {
            const point = name.mapToItem(dialog.background, 0, 0)
            return point.y >= 50 && point.y + name.height <= dialog.height - 72
        })
        height = 800; wait(20)
        height = 400
        tryVerify(() => {
            const point = name.mapToItem(dialog.background, 0, 0)
            return point.y >= 50 && point.y + name.height <= dialog.height - 72
        })
        dialog.urlsText = "https://one.test"
        dialog.submit(); dialog.finish(false)
        const urls = findChild(dialog, "networkUrlsField")
        tryVerify(() => urls.activeFocus)
        tryVerify(() => {
            const point = urls.mapToItem(dialog.background, 0, 0)
            return point.y >= 50 && point.y + urls.height <= dialog.height - 72
        })
        verify(findChild(dialog, "networkSaveButton").visible)
    }
    function test_multipleLinksKeepOrderAndOptionalName() {
        dialog.urlsText = " https://one.test/live.m3u \r\n\n rtsp://two.test/live\nhttps://one.test/live.m3u"
        dialog.playlistName = " TV Channels "
        mouseClick(findChild(dialog, "networkSaveButton"))
        compare(saveSpy.count, 1)
        compare(saveSpy.signalArguments[0][0], ["https://one.test/live.m3u", "rtsp://two.test/live", "https://one.test/live.m3u"])
        compare(saveSpy.signalArguments[0][1], "TV Channels")
        dialog.submit(); compare(saveSpy.count, 1)
        verify(dialog.visible)
        verify(!findChild(dialog, "networkCloseButton").enabled)
        dialog.finish(true)
        compare(acceptedSpy.count, 1)
        tryCompare(dialog, "visible", false)
    }
    function test_failureKeepsDraftForRetry() {
        dialog.urlsText = "udp://@239.0.0.1:1234"
        dialog.submit(); dialog.finish(false)
        verify(dialog.visible); verify(!dialog.busy)
        verify(dialog.errorMessage.length > 0)
        compare(dialog.urlsText, "udp://@239.0.0.1:1234")
        dialog.urlsText = "srt://one.test:9000"
        compare(dialog.errorMessage, "")
        dialog.submit(); compare(saveSpy.count, 2)
        compare(saveSpy.signalArguments[1][1], "")
    }
    function test_withoutLibraryOpensLinksWithoutSavingAPlaylist() {
        dialog.canSavePlaylist = false
        const name = findChild(dialog, "networkPlaylistNameField")
        const open = findChild(dialog, "networkSaveButton")
        verify(!name.enabled)
        compare(open.caption, "Open")
        verify(!open.enabled)
        dialog.urlsText = "https://one.test/live\nrtsp://two.test/live\nhttps://one.test/live"
        dialog.playlistName = "Old draft"
        mouseClick(open)
        compare(saveSpy.count, 1)
        compare(saveSpy.signalArguments[0][0], ["https://one.test/live", "rtsp://two.test/live", "https://one.test/live"])
        compare(saveSpy.signalArguments[0][1], "")
        compare(open.caption, "Opening…")
        dialog.finish(true)
        compare(acceptedSpy.count, 1)
    }
    function test_repeatedOpenKeepsDraftAndNewOpenClearsIt() {
        dialog.urlsText = "https://one.test"; dialog.playlistName = "Draft"
        dialog.begin(); compare(dialog.playlistName, "Draft")
        mouseClick(findChild(dialog, "networkCancelButton"))
        tryCompare(dialog, "visible", false)
        compare(saveSpy.count, 0)
        dialog.begin(); tryCompare(dialog, "opened", true)
        compare(dialog.urlsText, ""); compare(dialog.playlistName, "")
    }
    function test_keyboardSaveAndCancel() {
        dialog.urlsText = "https://one.test"
        keyClick(Qt.Key_Return, Qt.ControlModifier)
        compare(saveSpy.count, 1)
        keyClick(Qt.Key_Escape); verify(dialog.visible)
        dialog.finish(false)
        keyClick(Qt.Key_Escape); tryCompare(dialog, "visible", false)
    }
    function test_smallWindowAndScaleKeepButtonsVisible_data() {
        return [{tag: "small", w: 640, h: 400, scale: 1}, {tag: "scaled", w: 1280, h: 800, scale: 1.5}, {tag: "light", w: 1280, h: 800, scale: 1, light: true}]
    }
    function test_smallWindowAndScaleKeepButtonsVisible(data) {
        width = data.w; height = data.h
        dialog.scaleFactor = data.scale; dialog.lightTheme = !!data.light
        wait(50)
        verify(dialog.x >= 0); verify(dialog.y >= 0)
        verify(dialog.x + dialog.width <= width); verify(dialog.y + dialog.height <= height)
        for (const name of ["networkCloseButton", "networkSaveButton", "networkCancelButton"]) {
            const button = findChild(dialog, name)
            const point = button.mapToItem(testCase, 0, 0)
            verify(point.x >= 0 && point.y >= 0, name)
            verify(point.x + button.width <= width && point.y + button.height <= height, name)
        }
    }
}
