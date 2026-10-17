// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import VLC.MediaLibrary

Item {
    property alias streams: streams
    MLUrlModel {
        id: streams
        ml: MediaLib
    }
}
