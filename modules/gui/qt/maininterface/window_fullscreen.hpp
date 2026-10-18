// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_WINDOW_FULLSCREEN_HPP
#define VLC_WINDOW_FULLSCREEN_HPP

#include <QWindow>
#include <QVariant>

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <windows.h>

/* Qt restores the normal frame before calling SW_MAXIMIZE when leaving
 * fullscreen. Keep the fullscreen frame until the final maximize operation,
 * then retain Qt's requested normal frame for a later Restore action. */
class MaximizedRestoreGuard final : public QAbstractNativeEventFilter
{
public:
    MaximizedRestoreGuard(QWindow *window, Qt::WindowStates target)
    {
        const auto old = window->windowStates();
        if ((old.testFlag(Qt::WindowFullScreen) || old.testFlag(Qt::WindowMinimized)) &&
            target.testFlag(Qt::WindowMaximized) &&
            !target.testFlag(Qt::WindowFullScreen) &&
            !target.testFlag(Qt::WindowMinimized) && window->isVisible())
        {
            m_window = reinterpret_cast<HWND>(window->winId());
            if (window->flags().testFlag(Qt::FramelessWindowHint))
                m_cachedNormalFrame = window->property("__vlcNormalWindowPlacement").toRect();
            QCoreApplication::instance()->installNativeEventFilter(this);
        }
    }

    ~MaximizedRestoreGuard() override
    {
        if (!m_window)
            return;
        QCoreApplication::instance()->removeNativeEventFilter(this);
        if ((!m_hasNormalFrame && !m_cachedNormalFrame.isValid()) || !IsZoomed(m_window))
            return;

        WINDOWPLACEMENT placement = {};
        placement.length = sizeof(placement);
        if (!GetWindowPlacement(m_window, &placement))
            return;

        if (m_cachedNormalFrame.isValid())
        {
            // Qt's frameless maximize path can replace the native normal
            // placement. Retain the last actual windowed placement instead.
            placement.rcNormalPosition = {m_cachedNormalFrame.x(), m_cachedNormalFrame.y(),
                m_cachedNormalFrame.x() + m_cachedNormalFrame.width(),
                m_cachedNormalFrame.y() + m_cachedNormalFrame.height()};
        }
        else
        {
            // WINDOWPLACEMENT uses workspace coordinates for ordinary windows,
            // while WINDOWPOS and Qt's saved frame use screen coordinates.
            RECT normal = m_normalFrame;
            if (!(GetWindowLongPtr(m_window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW))
            {
                MONITORINFO monitor = {};
                monitor.cbSize = sizeof(monitor);
                if (GetMonitorInfoW(MonitorFromRect(&normal, MONITOR_DEFAULTTONEAREST), &monitor))
                    OffsetRect(&normal, monitor.rcMonitor.left - monitor.rcWork.left,
                                monitor.rcMonitor.top - monitor.rcWork.top);
            }
            placement.rcNormalPosition = normal;
        }
        SetWindowPlacement(m_window, &placement);
    }

    bool nativeEventFilter(const QByteArray&, void *message, qintptr *) override
    {
        const auto *event = static_cast<MSG *>(message);
        if (event->hwnd != m_window || event->message != WM_WINDOWPOSCHANGING ||
            IsZoomed(m_window))
            return false;

        auto *position = reinterpret_cast<WINDOWPOS *>(event->lParam);
        if ((position->flags & SWP_FRAMECHANGED) &&
            !(position->flags & (SWP_NOSIZE | SWP_NOMOVE)))
        {
            m_normalFrame = {position->x, position->y,
                             position->x + position->cx, position->y + position->cy};
            m_hasNormalFrame = true;
        }
        position->flags |= SWP_NOMOVE | SWP_NOSIZE;
        return false;
    }

private:
    HWND m_window = nullptr;
    RECT m_normalFrame = {};
    QRect m_cachedNormalFrame;
    bool m_hasNormalFrame = false;
};
#endif

inline void setWindowStatesPreservingGeometry(QWindow *window, Qt::WindowStates states)
{
#ifdef Q_OS_WIN
    const auto old = window->windowStates();
    const auto presentation = Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized;
    if (!(old & presentation) &&
        (states & (Qt::WindowMaximized | Qt::WindowFullScreen)))
    {
        WINDOWPLACEMENT placement = {};
        placement.length = sizeof(placement);
        if (GetWindowPlacement(reinterpret_cast<HWND>(window->winId()), &placement))
        {
            const auto& rect = placement.rcNormalPosition;
            window->setProperty("__vlcNormalWindowPlacement",
                QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top));
        }
    }
    MaximizedRestoreGuard guard(window, states);
#endif
    window->setWindowStates(states);
#ifdef Q_OS_WIN
    if ((old & presentation) && !(states & presentation) &&
        window->flags().testFlag(Qt::FramelessWindowHint) && window->isVisible())
    {
        const auto normal = window->property("__vlcNormalWindowPlacement").toRect();
        WINDOWPLACEMENT placement = {};
        placement.length = sizeof(placement);
        if (normal.isValid() && GetWindowPlacement(reinterpret_cast<HWND>(window->winId()), &placement))
        {
            placement.rcNormalPosition = {normal.x(), normal.y(),
                normal.x() + normal.width(), normal.y() + normal.height()};
            placement.showCmd = SW_SHOWNOACTIVATE;
            placement.flags &= ~WPF_RESTORETOMAXIMIZED;
            SetWindowPlacement(reinterpret_cast<HWND>(window->winId()), &placement);
        }
    }
#endif
}

#endif
