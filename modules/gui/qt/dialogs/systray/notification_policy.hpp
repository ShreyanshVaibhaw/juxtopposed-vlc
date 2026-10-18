// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_TRAY_NOTIFICATION_POLICY_HPP
#define VLC_QT_TRAY_NOTIFICATION_POLICY_HPP
#include "qt.hpp"
#include <QWindow>

namespace vlc::systray
{
inline bool shouldNotify(int policy, QWindow::Visibility visibility)
{
    return policy == NOTIFICATION_ALWAYS
        || (policy == NOTIFICATION_MINIMIZED
            && (visibility == QWindow::Hidden || visibility == QWindow::Minimized));
}
}
#endif
