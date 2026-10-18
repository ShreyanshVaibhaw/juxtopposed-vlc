// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedCompactMenus"
    width: 1100; height: 900
    visible: true; when: windowShown
    QtObject {
        id: redesignAppearance
        property bool compactMenus: false
        property bool backdropBlurEnabled: true
    }
    ListModel { id: recentFiles; Component.onCompleted: append({id: 42, url: "/videos/cat.mkv"}) }
    RedesignMenu {
        id: menu; parent: testCase
        recentModel: recentFiles
        profiles: [{id: "movies", name: "Movies"}]
        selectedProfile: "movies"
    }
    RedesignActionMenu {
        id: actions; parent: testCase
        entries: [{action: "play", label: "Play", icon: "play"},
                  {action: "shuffle", label: "Shuffle", icon: "shuffle", checked: true}]
    }
    RedesignComboBox {
        id: combo; x: 500; y: 20; width: 280
        model: [{label: "Flat", curve: [0,0,0]}, {label: "Bass", curve: [6,2,0]}]
        textRole: "label"; showCurvePreviews: true
    }
    QtObject {
        id: store
        property int revision: 0
        property var playerPresets: [{id: "movies", name: "Movies"}]
        property string selectedPlayerPreset: "movies"
        property string errorMessage: ""
        property bool pendingPlayerPreset: false
        function selectPlayerPreset(id) { selectedPlayerPreset = id; revision++ }
    }
    RedesignPlayerPreset { id: preset; x: 500; y: 650; width: 320; backend: store }
    RedesignTrackPopup { id: tracks; parent: testCase; placementArea: testCase }
    SignalSpy { id: triggered; target: actions; signalName: "actionRequested" }

    function init() {
        redesignAppearance.compactMenus = false
        menu.scaleFactor = actions.scaleFactor = combo.scaleFactor = preset.scaleFactor = tracks.scaleFactor = 1
        triggered.clear()
    }
    function cleanup() {
        menu.dismiss(); actions.dismiss(); combo.popup.close(); tracks.close()
        findChild(preset, "playerPresetActions").dismiss()
        findChild(preset, "playerPresetMenu").close()
        redesignAppearance.compactMenus = false
    }
    function test_mainAndNestedMenusUpdateLive() {
        menu.popup(testCase, Qt.point(20,20))
        tryCompare(menu, "opened", true)
        for (const compact of [true, false, true]) {
            redesignAppearance.compactMenus = compact
            compare(menu.width, compact ? 280 : 375)
            compare(menu.itemAt(0).implicitHeight, compact ? 28 : 48)
            compare(menu.itemAt(0).contentItem.children[0].visible, !compact)
            menu.currentIndex = 8
            menu.itemAt(8).forceActiveFocus()
            keyClick(Qt.Key_Right)
            const submenu = menu.itemAt(8).subMenu
            tryCompare(submenu, "opened", true)
            compare(submenu.itemAt(1).implicitHeight, compact ? 28 : 48)
            verify(submenu.itemAt(1).contentItem.children[0].visible)
            verify(submenu.itemAt(1).checked)
            keyClick(Qt.Key_Escape)
            tryCompare(submenu, "visible", false)
        }
    }
    function test_actionIconsHideAndSelectionMarksRemain() {
        actions.popup(testCase, Qt.point(20,20))
        tryCompare(actions, "opened", true)
        redesignAppearance.compactMenus = true
        compare(actions.itemAt(0).implicitHeight, 28)
        verify(!actions.itemAt(0).contentItem.children[0].visible)
        verify(actions.itemAt(1).contentItem.children[2].visible)
        compare(actions.itemAt(1).contentItem.children[2].opacity, 1)
        actions.itemAt(0).forceActiveFocus()
        keyClick(Qt.Key_Return)
        compare(triggered.count, 1)
        compare(triggered.signalArguments[0][0], "play")
    }
    function test_comboChoicesHidePreviewsAndKeepSelection() {
        combo.popup.open()
        tryCompare(combo.popup, "opened", true)
        const list = combo.popup.contentItem
        tryVerify(() => list.itemAtIndex(0) !== null)
        const row = list.itemAtIndex(0)
        compare(row.implicitHeight, 34)
        redesignAppearance.compactMenus = true
        compare(row.implicitHeight, 28)
        verify(!row.contentItem.children[0].visible)
        verify(row.contentItem.children[2].visible)
        compare(combo.currentIndex, 0)
        redesignAppearance.compactMenus = false
        compare(row.implicitHeight, 34)
        verify(row.contentItem.children[0].visible)
    }
    function test_presetActionsRemainAccessible() {
        redesignAppearance.compactMenus = true
        const dropdown = findChild(preset, "playerPresetMenu")
        dropdown.open()
        tryCompare(dropdown, "opened", true)
        compare(dropdown.contentItem.currentItem.height, 28)
        keyClick(Qt.Key_Right)
        const submenu = findChild(preset, "playerPresetActions")
        tryCompare(submenu, "opened", true)
        compare(submenu.itemAt(0).implicitHeight, 28)
        compare(submenu.itemAt(0).text, "Rename")
    }
    function test_tracksRetainSelectionAndTextActions() {
        redesignAppearance.compactMenus = true
        tracks.open()
        tryCompare(tracks, "opened", true)
        verify(tracks.height < 524)
        const embedded = findChild(tracks, "embeddedTrackChoices")
        compare(findChild(embedded, "disableTracks").implicitHeight, 28)
        const external = findChild(tracks, "externalTrackChoices")
        compare(findChild(external, "addExternalTracks").caption, "Add")
        compare(findChild(external, "addExternalTracks").iconName, "")
        verify(findChild(findChild(embedded, "disableTracks"), "selectionMark").visible)
    }
    function test_compactGeometryScales() {
        redesignAppearance.compactMenus = true
        menu.scaleFactor = actions.scaleFactor = 1.5
        compare(menu.width, 420)
        compare(menu.itemAt(0).implicitHeight, 42)
        actions.popup(testCase, Qt.point(20,20))
        tryCompare(actions, "opened", true)
        compare(actions.itemAt(0).implicitHeight, 42)
    }
}
