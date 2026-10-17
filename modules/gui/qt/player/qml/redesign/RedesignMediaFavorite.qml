// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

QtObject {
    id: root
    required property var service
    property url mediaUrl
    property bool favorite: false
    property bool busy: false
    property string mediaKey: ""
    property int generation: 0
    signal errorOccurred(string message)
    function current(request, url) { return request === generation && String(mediaUrl) === url }
    function refresh() {
        if (busy || !String(mediaUrl).length) return
        const owner = root, request = ++generation, url = String(mediaUrl)
        service.mediaState(mediaUrl, false, result => {
            if (!Qt.isQtObject(owner) || !owner.current(request, url)) return
            if (result.success) { owner.favorite = result.favorite; owner.mediaKey = result.id || "" }
        })
    }
    function toggle() {
        if (busy || !String(mediaUrl).length) return
        const owner = root, request = ++generation, url = String(mediaUrl)
        busy = true
        service.mediaState(mediaUrl, false, result => {
            if (!Qt.isQtObject(owner) || !owner.current(request, url)) return
            if (!result.success) { owner.busy = false; owner.errorOccurred(result.error); return }
            owner.service.setMediaState(url, "favorite", !result.favorite, saved => {
                if (!Qt.isQtObject(owner) || !owner.current(request, url)) return
                owner.busy = false
                if (saved.success) { owner.favorite = saved.favorite; owner.mediaKey = saved.id || "" }
                else owner.errorOccurred(saved.error)
            })
        })
    }
    onMediaUrlChanged: {
        ++generation; favorite = false; busy = false; mediaKey = ""
        Qt.callLater(refresh)
    }
    property Connections changes: Connections {
        target: root.service
        function onMediaStateChanged(id) { if (id === root.mediaKey) Qt.callLater(root.refresh) }
    }
    Component.onCompleted: Qt.callLater(refresh)
}
