// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <atomic>
#include <thread>
#include <vector>
#include "util/message_log_buffer.hpp"

static void append(MessageLogBuffer& buffer, const char* module, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    buffer.append(VLC_MSG_WARN, module, format, args);
    va_end(args);
}

static QString formatAfterObservation(MessageLogBuffer& buffer, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    buffer.append(VLC_MSG_INFO, "decoder", format, args);
    char full[32768];
    std::vsnprintf(full, sizeof(full), format, args);
    va_end(args);
    return QString::fromUtf8(full);
}

class TestMessageLogBuffer : public QObject
{
    Q_OBJECT
private slots:
    void formatAndUnicode()
    {
        MessageLogBuffer buffer;
        append(buffer, "decoder", "%d: %s", 42, "français العربية 中文 😺");
        auto batch = buffer.take();
        QCOMPARE(batch.entries.size(), 1);
        QCOMPARE(batch.entries[0].priority, VLC_MSG_WARN);
        QCOMPARE(batch.entries[0].module, QStringLiteral("decoder"));
        QCOMPARE(batch.entries[0].text, QString::fromUtf8("42: français العربية 中文 😺"));
        QCOMPARE(batch.skipped, 0);
        append(buffer, nullptr, "%s", "No module");
        QVERIFY(buffer.take().entries[0].module.isEmpty());
    }
    void boundedFormattingDoesNotSplitUnicode()
    {
        MessageLogBuffer buffer;
        const QByteArray text(MessageLogBuffer::MaximumTextBytes * 4, 'x');
        const QByteArray module(MessageLogBuffer::MaximumModuleBytes * 4, 'm');
        append(buffer, module.constData(), "%s", text.constData());
        auto entry = buffer.take().entries[0];
        QCOMPARE(entry.text.size(), qsizetype(MessageLogBuffer::MaximumTextBytes));
        QCOMPARE(entry.module.size(), qsizetype(MessageLogBuffer::MaximumModuleBytes + 1));
        QVERIFY(entry.text.endsWith(QChar(0x2026)));
        QVERIFY(entry.module.endsWith(QChar(0x2026)));
        const auto emoji = QByteArray::fromHex("f09f98ba");
        const QByteArray cutText = QByteArray(MessageLogBuffer::MaximumTextBytes - 2, 'x') + emoji;
        const QByteArray cutModule = QByteArray(MessageLogBuffer::MaximumModuleBytes - 1, 'm') + emoji;
        append(buffer, cutModule.constData(), "%s", cutText.constData());
        entry = buffer.take().entries[0];
        QVERIFY(!entry.text.contains(QChar::ReplacementCharacter));
        QVERIFY(!entry.module.contains(QChar::ReplacementCharacter));
        QVERIFY(entry.text.endsWith(QChar(0x2026)));
        QVERIFY(entry.module.endsWith(QChar(0x2026)));
    }
    void observationDoesNotConsumeArgumentsForOtherLoggers()
    {
        MessageLogBuffer buffer;
        const QByteArray text(MessageLogBuffer::MaximumTextBytes * 2, 'x');
        QCOMPARE(formatAfterObservation(buffer, "%d: %s", 42, text.constData()),
                 QStringLiteral("42: ") + QString::fromUtf8(text));
        const auto entry = buffer.take().entries[0];
        QVERIFY(entry.text.size() <= qsizetype(MessageLogBuffer::MaximumTextBytes));
        QVERIFY(entry.text.startsWith(QStringLiteral("42: ")));
        QVERIFY(entry.text.endsWith(QChar(0x2026)));
    }
    void overflowKeepsRecentEntriesAndReportsLossOnce()
    {
        MessageLogBuffer buffer;
        constexpr int count = 100000;
        for (int i = 0; i < count; ++i) append(buffer, "decoder", "%d", i);
        int expected = count - int(MessageLogBuffer::MaximumPending);
        int received = 0;
        quint64 skipped = 0;
        for (;;)
        {
            const auto batch = buffer.take();
            skipped += batch.skipped;
            if (batch.entries.isEmpty()) break;
            QVERIFY(size_t(batch.entries.size()) <= MessageLogBuffer::MaximumBatch);
            for (const auto& entry : batch.entries)
            {
                QCOMPARE(entry.text.toInt(), expected++);
                ++received;
            }
        }
        QCOMPARE(received, int(MessageLogBuffer::MaximumPending));
        QCOMPARE(skipped, quint64(count - received));
        QCOMPARE(buffer.take().skipped, 0);
    }
    void batchesBoundCountAndCharacters()
    {
        MessageLogBuffer buffer;
        for (int i = 0; i < 129; ++i) append(buffer, "decoder", "%d", i);
        QCOMPARE(buffer.take().entries.size(), qsizetype(MessageLogBuffer::MaximumBatch));
        QCOMPARE(buffer.take().entries.size(), 1);
        const QByteArray text(MessageLogBuffer::MaximumTextBytes * 2, 'x');
        for (int i = 0; i < 100; ++i) append(buffer, "decoder", "%s", text.constData());
        int received = 0;
        for (;;)
        {
            const auto batch = buffer.take();
            if (batch.entries.isEmpty()) break;
            qsizetype characters = 0;
            for (const auto& entry : batch.entries) characters += entry.module.size() + entry.text.size() + 16;
            QVERIFY(characters <= MessageLogBuffer::MaximumBatchCharacters);
            received += batch.entries.size();
        }
        QCOMPARE(received, 100);
    }
    void clearDiscardsPendingEntriesAndCounters()
    {
        MessageLogBuffer buffer;
        for (int i = 0; i < 2000; ++i) append(buffer, "decoder", "%d", i);
        buffer.clear();
        const auto batch = buffer.take();
        QVERIFY(batch.entries.isEmpty());
        QCOMPARE(batch.skipped, 0);
        append(buffer, "input", "New message");
        QCOMPARE(buffer.take().entries[0].text, QStringLiteral("New message"));
    }
    void concurrentProducersAndConsumer()
    {
        MessageLogBuffer buffer;
        std::atomic<int> finished{0};
        std::vector<std::thread> producers;
        for (int thread = 0; thread < 4; ++thread)
            producers.emplace_back([&, thread] {
                for (int i = 0; i < 10000; ++i) append(buffer, "decoder", "%d %d", thread, i);
                ++finished;
            });
        quint64 skipped = 0;
        int received = 0;
        int last[4] = {-1, -1, -1, -1};
        bool ordered = true;
        for (;;)
        {
            const bool done = finished.load() == 4;
            const auto batch = buffer.take();
            skipped += batch.skipped;
            for (const auto& entry : batch.entries)
            {
                const auto parts = entry.text.split(QLatin1Char(' '));
                if (parts.size() != 2) { ordered = false; continue; }
                const int producer = parts[0].toInt(), sequence = parts[1].toInt();
                if (producer < 0 || producer >= 4) { ordered = false; continue; }
                ordered &= sequence > last[producer];
                last[producer] = sequence;
                ++received;
            }
            if (done && batch.entries.isEmpty()) break;
            std::this_thread::yield();
        }
        for (auto& thread : producers) thread.join();
        QVERIFY(ordered);
        QCOMPARE(quint64(received) + skipped, 40000);
    }
};

QTEST_APPLESS_MAIN(TestMessageLogBuffer)
#include "test_message_log_buffer.moc"
