// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    name: "JuxtopposedMediaFavorite"
    QtObject {
        id: service
        property var reads: []
        property var writes: []
        signal mediaStateChanged(string id)
        function mediaState(url, create, callback) { reads.push({url: String(url), create: create, callback: callback}) }
        function setMediaState(url, field, value, callback) { writes.push({url: String(url), field: field, value: value, callback: callback}) }
    }
    RedesignMediaFavorite { id: favorite; service: service }
    Component { id: favoriteFactory; RedesignMediaFavorite { } }
    SignalSpy { id: errorSpy; target: favorite; signalName: "errorOccurred" }
    function init() {
        favorite.mediaUrl = ""; wait(1)
        service.reads = []; service.writes = []; errorSpy.clear()
        favorite.mediaUrl = "file:///one.mkv"
        tryCompare(service.reads, "length", 1)
    }
    function test_refreshDoesNotCreateLibraryMedia() {
        compare(service.reads[0].create, false)
        service.reads[0].callback({success: true, favorite: true, id: "one"})
        verify(favorite.favorite); compare(favorite.mediaKey, "one")
        compare(service.writes.length, 0)
    }
    function test_toggleUsesCurrentStateAndLocksDuringWrite() {
        service.reads[0].callback({success: true, favorite: true, id: "one"})
        favorite.toggle(); favorite.toggle()
        compare(service.reads.length, 2); verify(favorite.busy)
        // A library action changed this item since the first read.
        service.reads[1].callback({success: true, favorite: false, id: "one"})
        compare(service.writes.length, 1)
        compare(service.writes[0].field, "favorite"); compare(service.writes[0].value, true)
        service.writes[0].callback({success: true, favorite: true, id: "one"})
        verify(favorite.favorite); verify(!favorite.busy)
    }
    function test_oldReadDoesNotChangeNewInput() {
        favorite.mediaUrl = "file:///two.mkv"
        tryCompare(service.reads, "length", 2)
        service.reads[0].callback({success: true, favorite: true, id: "one"})
        verify(!favorite.favorite)
        service.reads[1].callback({success: true, favorite: false, id: "two"})
        compare(favorite.mediaKey, "two")
    }
    function test_oldWriteDoesNotChangeNewInput() {
        favorite.toggle()
        service.reads[1].callback({success: true, favorite: false, id: "one"})
        favorite.mediaUrl = "file:///two.mkv"
        tryCompare(service.reads, "length", 3)
        service.writes[0].callback({success: true, favorite: true, id: "one"})
        verify(!favorite.favorite); verify(!favorite.busy)
    }
    function test_externalChangesOnlyRefreshCurrentMedia() {
        service.reads[0].callback({success: true, favorite: true, id: "one"})
        service.mediaStateChanged("other"); wait(1); compare(service.reads.length, 1)
        service.mediaStateChanged("one"); service.mediaStateChanged("one")
        tryCompare(service.reads, "length", 2)
        service.reads[1].callback({success: true, favorite: false, id: "one"})
        verify(!favorite.favorite)
    }
    function test_failureKeepsStateAndAllowsRetry() {
        service.reads[0].callback({success: true, favorite: true, id: "one"})
        favorite.toggle(); service.reads[1].callback({success: false, error: "Cannot read"})
        verify(favorite.favorite); verify(!favorite.busy); compare(errorSpy.count, 1)
        favorite.toggle(); service.reads[2].callback({success: true, favorite: true, id: "one"})
        service.writes[0].callback({success: false, error: "Cannot save"})
        verify(favorite.favorite); verify(!favorite.busy); compare(errorSpy.count, 2)
    }
    function test_removedControlIgnoresPendingRead() {
        const doomed = createTemporaryObject(favoriteFactory, this, {service: service, mediaUrl: "file:///removed.mkv"})
        verify(doomed !== null)
        tryCompare(service.reads, "length", 2)
        const callback = service.reads[1].callback
        doomed.destroy(); wait(1)
        callback({success: true, favorite: true, id: "removed"})
        compare(service.writes.length, 0)
    }
    function test_removedControlIgnoresPendingWrite() {
        const doomed = createTemporaryObject(favoriteFactory, this, {service: service, mediaUrl: "file:///removed.mkv"})
        verify(doomed !== null)
        tryCompare(service.reads, "length", 2)
        doomed.toggle()
        service.reads[2].callback({success: true, favorite: false, id: "removed"})
        compare(service.writes.length, 1)
        const callback = service.writes[0].callback
        doomed.destroy(); wait(1)
        callback({success: true, favorite: true, id: "removed"})
        verify(!favorite.favorite)
    }
}
