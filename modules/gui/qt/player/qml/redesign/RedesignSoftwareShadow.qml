// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import VLC.Util

Item {
    id: root
    property real scaleFactor: 1
    property real cornerRadius: 10 * scaleFactor
    readonly property real sigma: 7.5 * scaleFactor
    readonly property real offset: 8 * scaleFactor
    readonly property real padding: Math.ceil(sigma * 3 + offset)

    Image {
        objectName: "softwareMenuShadowImage"
        x: -root.padding; y: -root.padding
        width: root.width + 2 * root.padding
        height: root.height + 2 * root.padding
        asynchronous: true; cache: true
        fillMode: Image.Stretch
        // Qt applies the window's device pixel ratio to provider requests.
        sourceSize: Qt.size(Math.ceil(width), Math.ceil(height))
        source: root.width > 0 && root.height > 0 ? Effects.url(Effects.RoundedRectDropShadow, {
            viewportWidth: width, viewportHeight: height,
            rectWidth: root.width, rectHeight: root.height,
            blurRadius: root.sigma * 2, yOffset: root.offset,
            color: Qt.rgba(0, 0, 0, 0.46),
            xRadius: root.cornerRadius, yRadius: root.cornerRadius,
            exclusionX: root.padding, exclusionY: root.padding,
            exclusionWidth: root.width, exclusionHeight: root.height,
            exclusionRadius: root.cornerRadius
        }) : ""
    }
}
