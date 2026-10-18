// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedFullscreenCursor"
    width: 1000; height: 700
    visible: true; when: windowShown
    property bool consumeVideoHover: false
    RedesignPlayerView {
        id: player
        anchors.fill: parent
        started: true; hasVideo: true
        videoContent: MouseArea {
            hoverEnabled: true
            enabled: testCase.consumeVideoHover
            cursorShape: player.videoCursorVisible ? Qt.ArrowCursor : Qt.BlankCursor
        }
    }
    function init() {
        player.fullscreen = false
        player.controlsHideTimeout = 40
        player.cursorHideTimeout = 150
        player.fullscreenControlsSensitivity = 32
        player.showFullscreenControls = true
        player.settingsPage = ""; player.queueOpen = false
        consumeVideoHover = false
        player.forceActiveFocus()
        mouseMove(player, 450, 300)
    }
    function test_movementRestoresCursorWithoutRevealingBars_data() {
        return [{tag: "plain", consume: false}, {tag: "video accepts hover", consume: true},
                {tag: "controls disabled", consume: true, controls: false}]
    }
    function test_movementRestoresCursorWithoutRevealingBars(data) {
        consumeVideoHover = data.consume
        player.showFullscreenControls = data.controls !== false
        player.fullscreen = true
        tryCompare(player, "videoCursorVisible", false)
        verify(!player.topVisible); verify(!player.bottomVisible)
        // Even one pixel of movement must restore the cursor, regardless of
        // the edge sensitivity used to reveal the control bars.
        mouseMove(player, 451, 300)
        compare(player.videoCursorVisible, true)
        compare(player.videoItem.cursorShape, Qt.ArrowCursor)
        verify(!player.topVisible); verify(!player.bottomVisible)
        wait(90)
        mouseMove(player, 452, 300)
        wait(90)
        verify(player.videoCursorVisible, "Movement restarts the idle delay")
        tryCompare(player, "videoCursorVisible", false)
        compare(player.videoItem.cursorShape, Qt.BlankCursor)
    }
    function test_leavingEitherPanelKeepsCursorUntilIdle_data() {
        return [{tag: "header", y: 2}, {tag: "toolbar", y: 698}]
    }
    function test_leavingEitherPanelKeepsCursorUntilIdle(data) {
        consumeVideoHover = true
        player.fullscreen = true
        tryCompare(player, "videoCursorVisible", false)
        mouseMove(player, 500, data.y)
        compare(player.videoCursorVisible, true)
        mouseMove(player, 520, 330)
        compare(player.videoCursorVisible, true)
        compare(player.videoItem.cursorShape, Qt.ArrowCursor)
        tryCompare(player, "videoCursorVisible", false)
    }
    function test_cursorIdleIsIndependentOfControlsAndPanels() {
        player.controlsHideTimeout = 5000
        player.fullscreen = true
        verify(player.topVisible); verify(player.bottomVisible)
        tryCompare(player, "videoCursorVisible", false)
        verify(player.topVisible); verify(player.bottomVisible)
        player.settingsPage = "playback"
        mouseMove(player, 460, 310)
        compare(player.videoCursorVisible, true)
        tryCompare(player, "videoCursorVisible", false)
        verify(player.topVisible); verify(player.bottomVisible)
    }
    function test_windowedCursorAndFullscreenReentry() {
        consumeVideoHover = true
        player.fullscreen = true
        tryCompare(player, "videoCursorVisible", false)
        player.fullscreen = false
        compare(player.videoItem.cursorShape, Qt.ArrowCursor)
        wait(180)
        verify(player.videoCursorVisible)
        player.fullscreen = true
        compare(player.videoItem.cursorShape, Qt.ArrowCursor)
        tryCompare(player, "videoCursorVisible", false)
    }
}
