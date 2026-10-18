// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_FILE_LOG_PREFERENCES_HPP
#define VLC_QT_FILE_LOG_PREFERENCES_HPP
#include <QString>

namespace vlc::filelog
{
// The untouched app control uses the design default. An existing native file
// logger must show what its inherited verbosity actually includes.
inline QString selection(const QString &level, bool enabled, qint64 fileVerbosity, qint64 verbosity)
{
    if (!level.isEmpty()) return level;
    if (!enabled) return QStringLiteral("errors");
    const auto inherited = fileVerbosity == -1 ? verbosity : fileVerbosity;
    return inherited < 0 ? QStringLiteral("silent") : inherited == 0 ? QStringLiteral("native-info-errors")
        : inherited == 1 ? QStringLiteral("info") : QStringLiteral("debug");
}
}
#endif
