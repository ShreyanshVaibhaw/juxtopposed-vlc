// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_WARNING_LOG_BUFFER_HPP
#define VLC_QT_WARNING_LOG_BUFFER_HPP

#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <utility>

// Log callbacks run on producer threads. Keep one bounded sample per UI batch;
// a log flood must not allocate a GUI event or string for every warning.
class WarningLogBuffer
{
public:
    struct Batch
    {
        QString module;
        QString text;
        int count = 0;
    };

    void append(const char *module, const char *format, va_list args)
    {
        QMutexLocker lock(&m_mutex);
        if (m_pending.count == 0)
        {
            char source[129];
            char message[2048];
            std::snprintf(source, sizeof(source), "%s", module ? module : "");
            const int length = std::vsnprintf(message, sizeof(message), format, args);
            m_pending.module = QString::fromUtf8(source);
            m_pending.text = length < 0 ? QString() : QString::fromUtf8(message);
        }
        if (m_pending.count < INT_MAX)
            ++m_pending.count;
    }

    Batch take()
    {
        QMutexLocker lock(&m_mutex);
        return std::exchange(m_pending, {});
    }

    void clear()
    {
        QMutexLocker lock(&m_mutex);
        m_pending = {};
    }

private:
    QMutex m_mutex;
    Batch m_pending;
};
#endif
