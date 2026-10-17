// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

Image {
    id: root
    objectName: "seekThumbnailImage"
    property url frameSource
    property real scaleFactor: 1
    readonly property bool preparedFrame: String(frameSource).startsWith("image://seekpreview/")
    cache: false
    asynchronous: true
    fillMode: preparedFrame ? Image.Stretch : Image.PreserveAspectFit
    // Qt applies the window's pixel ratio to the provider request.
    sourceSize: Qt.size(Math.ceil(width), Math.ceil(height))
    source: preparedFrame ? width > 0 && height > 0
        ? String(frameSource) + "?rounded=1&radius=" + (4 * scaleFactor) + "&width=" + Math.ceil(width) : ""
        : frameSource
}
