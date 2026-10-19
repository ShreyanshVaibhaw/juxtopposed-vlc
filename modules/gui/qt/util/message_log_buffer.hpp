// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_MESSAGE_LOG_BUFFER_HPP
#define VLC_QT_MESSAGE_LOG_BUFFER_HPP

#include <vlc_common.h>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QStringDecoder>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <limits>
#include <utility>

struct MessageLogEntry
{
    int priority = VLC_MSG_INFO;
    QString module;
    QString text;
};

// Producer callbacks must not allocate an event or an unbounded string for
// every log record. Keep recent entries and let the GUI take bounded batches.
class MessageLogBuffer
{
public:
    static constexpr size_t MaximumPending = 1024;
    static constexpr size_t MaximumBatch = 128;
    static constexpr qsizetype MaximumBatchCharacters = 32768;
    static constexpr size_t MaximumTextBytes = 8192;
    static constexpr size_t MaximumModuleBytes = 128;

    struct Batch
    {
        QList<MessageLogEntry> entries;
        quint64 skipped = 0;
    };

    void append(int priority, const char* module, const char* format, va_list args)
    {
        std::array<char, MaximumTextBytes> text;
        va_list copy;
        va_copy(copy, args);
        const int length = std::vsnprintf(text.data(), text.size(), format, copy);
        va_end(copy);
        if (length < 0) return;
        const bool shortened = size_t(length) >= text.size();
        QStringDecoder decoder(QStringDecoder::Utf8);
        QString decoded = decoder.decode(QByteArrayView(text.data(), shortened ? text.size() - 1 : size_t(length)));
        if (shortened) decoded.append(QChar(0x2026));
        size_t moduleLength = 0;
        while (module && moduleLength < MaximumModuleBytes && module[moduleLength]) ++moduleLength;
        QStringDecoder sourceDecoder(QStringDecoder::Utf8);
        QString source = module ? QString(sourceDecoder.decode(QByteArrayView(module, moduleLength))) : QString();
        if (module && moduleLength == MaximumModuleBytes && module[moduleLength]) source.append(QChar(0x2026));

        QMutexLocker lock(&m_mutex);
        if (m_pending.size() == MaximumPending)
        {
            m_pending.pop_front();
            if (m_skipped != std::numeric_limits<quint64>::max()) ++m_skipped;
        }
        m_pending.push_back({priority, std::move(source), std::move(decoded)});
    }

    Batch take()
    {
        QMutexLocker lock(&m_mutex);
        Batch batch;
        batch.skipped = std::exchange(m_skipped, 0);
        qsizetype characters = 0;
        while (!m_pending.empty() && size_t(batch.entries.size()) < MaximumBatch)
        {
            const auto& entry = m_pending.front();
            const qsizetype cost = entry.module.size() + entry.text.size() + 16;
            if (characters + cost > MaximumBatchCharacters) break;
            characters += cost;
            batch.entries.append(std::move(m_pending.front()));
            m_pending.pop_front();
        }
        return batch;
    }

    void clear()
    {
        QMutexLocker lock(&m_mutex);
        m_pending.clear();
        m_skipped = 0;
    }

private:
    QMutex m_mutex;
    std::deque<MessageLogEntry> m_pending;
    quint64 m_skipped = 0;
};

#endif
