// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts

Item {
    id: root
    required property RedesignTheme theme
    required property RedesignPlayerLayout model
    required property Item headerHost
    required property Item footerHost
    required property var controls
    property Component extraControlComponent
    property var extras: ({})
    readonly property var allControls: Object.assign({}, controls, extras)
    property var renderedLayout: model.defaults()
    readonly property bool custom: JSON.stringify(renderedLayout) !== JSON.stringify(model.defaults())
    readonly property real controlsHeight: theme.dp(custom && renderedLayout.zones.above.length ? 108 : 54)
    property var originalParents: ({})
    property bool ready: false
    property bool applying: false
    visible: false

    function hasTimeline(zone) { return renderedLayout.zones[zone].includes("timeline") }
    function hasSpacer(zone) { return renderedLayout.zones[zone].some(id => id.startsWith("spacer:")) }
    function hasStretch(zone) { return hasTimeline(zone) || hasSpacer(zone) }
    function preferredWidth(zone) {
        let width = 0, count = 0
        for (const id of renderedLayout.zones[zone]) {
            const control = allControls[id]
            if (!control) continue
            width += (control.implicitWidth || control.width) + control.Layout.leftMargin + control.Layout.rightMargin
            ++count
        }
        return width + Math.max(0, count - 1) * theme.dp(10)
    }
    function schedule() { if (ready) Qt.callLater(apply) }
    function apply() {
        const targets = {headerLeft: headerLeft, headerCenter: headerCenter, headerRight: headerRight,
            footerLeft: footerLeft, footerCenter: footerCenter, footerRight: footerRight, above: above}
        const next = model.clone(model.layout)
        const isCustom = JSON.stringify(next) !== JSON.stringify(model.defaults())
        applying = true
        const extra = Object.assign({}, extras)
        for (const zone of model.zones)
            for (const id of next.zones[zone])
                if (!controls[id] && !extra[id] && extraControlComponent)
                    extra[id] = extraControlComponent.createObject(root, {controlId: id})
        extras = extra
        // Keep the real controls alive: seeking, focus, menus and volume input
        // must keep their state when a layout moves a control to another bar.
        for (const id of Object.keys(allControls)) allControls[id].parent = root
        if (!isCustom) {
            for (const id of Object.keys(controls)) controls[id].parent = originalParents[id]
        } else {
            for (const zone of model.zones)
                for (const id of next.zones[zone])
                    if (allControls[id]) allControls[id].parent = targets[zone]
        }
        renderedLayout = next
        applying = false
    }
    Connections { target: root.model; function onLayoutChanged() { root.schedule() } }
    Component.onCompleted: {
        const parents = {}
        for (const id of Object.keys(controls)) parents[id] = controls[id].parent
        originalParents = parents
        ready = true
        schedule()
    }

    Item {
        parent: root.headerHost
        anchors.fill: parent
        visible: root.custom && !root.applying
        RowLayout {
            id: headerLeft
            objectName: "customHeaderLeft"
            x: root.theme.dp(50)
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
        }
        RowLayout {
            id: headerCenter
            objectName: "customHeaderCenter"
            anchors.centerIn: parent
            width: Math.max(0, parent.width - Math.max(headerLeft.x + headerLeft.width,
                parent.width - headerRight.x) * 2 - root.theme.dp(32))
            spacing: root.theme.dp(10)
        }
        RowLayout {
            id: headerRight
            objectName: "customHeaderRight"
            anchors.right: parent.right; anchors.rightMargin: root.allControls.close && root.allControls.close.flushToCorner ? 0 : root.theme.dp(6)
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
        }
    }
    Item {
        parent: root.footerHost
        anchors.bottom: parent.bottom
        width: parent.width; height: root.controlsHeight
        visible: root.custom && !root.applying
        RowLayout {
            id: above
            objectName: "customTimelineRow"
            x: root.theme.dp(20); y: root.theme.dp(1) + (root.theme.dp(54) - height) / 2
            width: parent.width - root.theme.dp(40)
            spacing: root.theme.dp(10)
        }
        Item {
            anchors.bottom: parent.bottom
            width: parent.width; height: root.theme.dp(54)
            RowLayout {
                id: footerLeft
                objectName: "customFooterLeft"
                x: root.theme.dp(18)
                width: root.hasSpacer("footerLeft") && !root.hasStretch("footerCenter")
                    ? Math.max(implicitWidth, (parent.width - root.preferredWidth("footerCenter")) / 2 - root.theme.dp(28)) : implicitWidth
                anchors.verticalCenter: parent.verticalCenter
                spacing: root.theme.dp(10)
            }
            RowLayout {
                id: footerCenter
                objectName: "customFooterCenter"
                x: root.hasStretch("footerCenter") ? footerLeft.x + footerLeft.width + root.theme.dp(10) : (parent.width - width) / 2
                width: root.hasStretch("footerCenter") ? Math.max(0, parent.width - footerLeft.width - footerRight.width - root.theme.dp(56)) : root.preferredWidth("footerCenter")
                anchors.verticalCenter: parent.verticalCenter
                spacing: root.theme.dp(10)
            }
            RowLayout {
                id: footerRight
                objectName: "customFooterRight"
                anchors.right: parent.right; anchors.rightMargin: root.theme.dp(18)
                width: root.hasSpacer("footerRight") && !root.hasStretch("footerCenter")
                    ? Math.max(implicitWidth, (parent.width - root.preferredWidth("footerCenter")) / 2 - root.theme.dp(28)) : implicitWidth
                anchors.verticalCenter: parent.verticalCenter
                spacing: root.theme.dp(10)
            }
        }
    }
}
