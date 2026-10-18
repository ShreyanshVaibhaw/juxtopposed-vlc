// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_WINDOW_CSD_GEOMETRY_HPP
#define VLC_WINDOW_CSD_GEOMETRY_HPP

#include <QWindow>
#include <QVariant>
#include <windows.h>

inline bool adjustCSDClientRect(QWindow *window, bool platformHandlesResize,
                                int borderWidth, int borderHeight, RECT *clientRect)
{
    // WindowStateHolder updates the fullscreen request before Qt sends native
    // frame messages. QWindow::windowState() can still report fullscreen while
    // restoring the maximized frame, so use the requested state for its margins.
    const auto fullscreenRequest = window->property("__windowFullScreen");
    const bool fullscreen = fullscreenRequest.isValid() ? fullscreenRequest.toUInt() != 0
        : window->windowStates().testFlag(Qt::WindowFullScreen);
    if (fullscreen)
        return false;

    if (IsZoomed(reinterpret_cast<HWND>(window->winId())))
    {
        // A maximized native frame extends beyond the work area by the resize
        // border on every side. Keep the entire client area inside those edges.
        clientRect->top += borderHeight;
        clientRect->bottom -= borderHeight;
        clientRect->left += borderWidth;
        clientRect->right -= borderWidth;
        return true;
    }
    if (platformHandlesResize)
    {
        // Leave the top resize handle inside the custom caption, as with SSD.
        clientRect->left += borderWidth;
        clientRect->right -= borderWidth;
        clientRect->bottom -= borderHeight;
        return true;
    }
    return false;
}

#endif
