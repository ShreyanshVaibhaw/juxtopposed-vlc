// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_VIDEO_WINDOW_GEOMETRY_HPP
#define VLC_QT_VIDEO_WINDOW_GEOMETRY_HPP

#include <QSize>
#include <algorithm>

namespace vlc::video
{
inline QSize fitWindowSize(const QSize& video, const QSize& chrome, const QSize& available,
                          const QSize& minimum, const QSize& maximum)
{
    if (video.isEmpty() || available.isEmpty())
        return {};
    const QSize limit(std::max(minimum.width(), std::min(maximum.width(), available.width())),
                      std::max(minimum.height(), std::min(maximum.height(), available.height())));
    const QSize canvas(std::max(1, limit.width() - chrome.width()),
                       std::max(1, limit.height() - chrome.height()));
    const auto fitted = video.width() > canvas.width() || video.height() > canvas.height()
        ? video.scaled(canvas, Qt::KeepAspectRatio) : video;
    return QSize(std::clamp(fitted.width() + chrome.width(), minimum.width(), limit.width()),
                 std::clamp(fitted.height() + chrome.height(), minimum.height(), limit.height()));
}
}
#endif
