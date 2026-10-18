// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <thread>
#include <vector>
#include "util/warning_log_buffer.hpp"

static void append(WarningLogBuffer &buffer, const char *module, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    buffer.append(module, format, args);
    va_end(args);
}

class TestWarningLogBuffer : public QObject
{
    Q_OBJECT
private slots:
    void format()
    {
        WarningLogBuffer buffer;
        append(buffer, "decoder", "Frame %d: %s", 42, "late");
        const auto batch = buffer.take();
        QCOMPARE(batch.module, "decoder");
        QCOMPARE(batch.text, "Frame 42: late");
        QCOMPARE(batch.count, 1);
        QCOMPARE(buffer.take().count, 0);
    }
    void coalesce()
    {
        WarningLogBuffer buffer;
        append(buffer, "first", "First warning");
        append(buffer, "second", "Other warning");
        const auto batch = buffer.take();
        QCOMPARE(batch.count, 2);
        QCOMPARE(batch.module, "first");
        QCOMPARE(batch.text, "First warning");
    }
    void flood()
    {
        WarningLogBuffer buffer;
        for (int i = 0; i < 100000; ++i)
            append(buffer, "decoder", "Late frame %d", i);
        const auto batch = buffer.take();
        QCOMPARE(batch.count, 100000);
        QCOMPARE(batch.text, "Late frame 0");
        QCOMPARE(buffer.take().count, 0);
    }
    void boundedText()
    {
        WarningLogBuffer buffer;
        const QByteArray longText(100000, 'x');
        append(buffer, longText.constData(), "%s", longText.constData());
        const auto batch = buffer.take();
        QCOMPARE(batch.module.size(), 128);
        QCOMPARE(batch.text.size(), 2047);
        QCOMPARE(batch.count, 1);
    }
    void unicode()
    {
        WarningLogBuffer buffer;
        append(buffer, "audio", "%s", "Ton für links");
        QCOMPARE(buffer.take().text, QString::fromUtf8("Ton für links"));
    }
    void clear()
    {
        WarningLogBuffer buffer;
        append(buffer, "decoder", "Old warning");
        buffer.clear();
        QCOMPARE(buffer.take().count, 0);
        append(buffer, "audio", "New warning");
        const auto batch = buffer.take();
        QCOMPARE(batch.module, "audio");
        QCOMPARE(batch.text, "New warning");
        QCOMPARE(batch.count, 1);
    }
    void noModule()
    {
        WarningLogBuffer buffer;
        append(buffer, nullptr, "%s", "Warning");
        const auto batch = buffer.take();
        QVERIFY(batch.module.isEmpty());
        QCOMPARE(batch.text, "Warning");
    }
    void concurrent()
    {
        WarningLogBuffer buffer;
        std::vector<std::thread> producers;
        for (int thread = 0; thread < 4; ++thread)
            producers.emplace_back([&buffer, thread] {
                for (int i = 0; i < 5000; ++i)
                    append(buffer, "decoder", "Thread %d frame %d", thread, i);
            });
        int total = 0;
        // Consume while the producers write, as the GUI does during playback.
        for (int i = 0; i < 1000; ++i)
            total += buffer.take().count;
        for (auto &thread : producers)
            thread.join();
        total += buffer.take().count;
        QCOMPARE(total, 20000);
    }
};
QTEST_APPLESS_MAIN(TestWarningLogBuffer)
#include "test_warning_log_buffer.moc"
