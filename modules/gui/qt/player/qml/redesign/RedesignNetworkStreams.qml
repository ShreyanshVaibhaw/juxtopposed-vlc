// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import VLC.MainInterface
import VLC.Playlist
import VLC.Style

RedesignNetworkDialog {
    id: root
    scaleFactor: VLCStyle.scale
    lightTheme: !VLCStyle.palette.isDark
    accentColor: MainCtx.redesignAccent
    canSavePlaylist: MainCtx.mediaLibraryAvailable
    Binding {
        target: MainCtx
        property: "useGlobalShortcuts"
        value: false
        when: root.visible
        restoreMode: Binding.RestoreBindingOrValue
    }
    onSaveRequested: (urls, name) => {
        if (canSavePlaylist) {
            library.item.streams.openStreams(urls, name)
        } else {
            MainPlaylistController.append(urls, true)
            root.finish(true)
        }
    }
    Loader {
        id: library
        active: root.canSavePlaylist
        source: active ? "qrc:///qt/qml/VLC/Player/RedesignNetworkStreamLibrary.qml" : ""
    }
    Connections {
        target: library.item ? library.item.streams : null
        function onStreamsOpened(success) { root.finish(success) }
    }
}
