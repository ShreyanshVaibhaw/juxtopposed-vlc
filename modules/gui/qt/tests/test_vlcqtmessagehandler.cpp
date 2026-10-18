// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <QTest>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <thread>

#include <vlc_common.h>
#include <vlc_interface.h>
#include <vlc_variables.h>
#include "../../../../lib/libvlc_internal.h"
#include "util/vlcqtmessagehandler.hpp"

extern "C" const char vlc_module_name[] = "qt_message_test";

class TestVlcQtMessageHandler : public QObject
{
    Q_OBJECT
    struct Record { int level; QString text; };
    QMutex m_mutex;
    QList<Record> m_logs;
    QStringList m_fallback;
    libvlc_instance_t *m_instance = nullptr;
    QtMessageHandler m_previous = nullptr;
    bool m_registered = false;
    static TestVlcQtMessageHandler *s_test;

    static void log(void *data, int level, const vlc_log_t *, const char *format, va_list args)
    {
        char *text;
        if (vasprintf(&text, format, args) < 0) return;
        auto *test = static_cast<TestVlcQtMessageHandler *>(data);
        QMutexLocker lock(&test->m_mutex);
        test->m_logs.append({level, QString::fromUtf8(text)});
        free(text);
    }
    static void fallback(QtMsgType, const QMessageLogContext &, const QString &message)
    {
        if (!s_test) return;
        QMutexLocker lock(&s_test->m_mutex);
        s_test->m_fallback.append(message);
    }
    bool contains(const QString &text, int level)
    {
        QMutexLocker lock(&m_mutex);
        for (const auto &record : m_logs)
            if (record.text.contains(text) && record.level == level) return true;
        return false;
    }
    int matching(const QString &prefix)
    {
        QMutexLocker lock(&m_mutex);
        int count = 0;
        for (const auto &record : m_logs) if (record.text.contains(prefix)) ++count;
        for (const auto &record : m_fallback) if (record.contains(prefix)) ++count;
        return count;
    }

private slots:
    void init()
    {
        const char *args[] = {"--no-media-library", "--no-metadata-network-access", "--no-plugins-cache"};
        m_instance = libvlc_new(ARRAY_SIZE(args), args);
        QVERIFY(m_instance);
        auto *object = VLC_OBJECT(m_instance->p_libvlc_int);
        var_Create(object, "qt-verbose", VLC_VAR_BOOL);
        var_SetBool(object, "qt-verbose", false);
        static const vlc_logger_operations ops = {log, nullptr};
        vlc_LogSet(m_instance->p_libvlc_int, &ops, this);
        s_test = this;
        m_previous = qInstallMessageHandler(fallback);
        setupVlcQtMessageHandler(object);
        m_registered = true;
    }
    void cleanup()
    {
        if (m_registered) cleanupVlcQtMessageHandler();
        m_registered = false;
        qInstallMessageHandler(m_previous);
        s_test = nullptr;
        if (m_instance) {
            vlc_LogSet(m_instance->p_libvlc_int, nullptr, nullptr);
            libvlc_release(m_instance);
            m_instance = nullptr;
        }
        m_logs.clear(); m_fallback.clear();
    }
    void testEssentialMessages_data()
    {
        QTest::addColumn<QString>("category");
        for (const auto *category : {"default", "qml", "js", "vlc.test", "qt.rhi.general"})
            QTest::newRow(category) << QString::fromUtf8(category);
    }
    void testEssentialMessages()
    {
        QFETCH(QString, category);
        const QByteArray name = category.toUtf8();
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO, name.constData()).warning("essential-message");
        QVERIFY(contains(QStringLiteral("essential-message"), VLC_MSG_WARN));
        QVERIFY(m_fallback.isEmpty());
    }
    void testLiveRouting()
    {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO, "qt.internal.test").warning("off-before");
        QVERIFY(!contains(QStringLiteral("off-before"), VLC_MSG_WARN));
        QVERIFY(m_fallback.contains(QStringLiteral("off-before")));
        updateVlcQtMessageHandler(true);
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO, "qt.internal.test").warning("on-warning");
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO, "qt.internal.test").info("on-info");
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO, "qt.internal.test").critical("on-error");
        QVERIFY(contains(QStringLiteral("on-warning"), VLC_MSG_WARN));
        QVERIFY(contains(QStringLiteral("on-info"), VLC_MSG_INFO));
        QVERIFY(contains(QStringLiteral("on-error"), VLC_MSG_ERR));
        updateVlcQtMessageHandler(false);
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO, "qt.internal.test").warning("off-after");
        QVERIFY(!contains(QStringLiteral("off-after"), VLC_MSG_WARN));
        QVERIFY(m_fallback.contains(QStringLiteral("off-after")));
    }
    void testCategoryRules()
    {
        QLoggingCategory category("qt.internal.policytest", QtWarningMsg);
        QLoggingCategory application("vlc.internal.policytest", QtWarningMsg);
        QVERIFY(!category.isInfoEnabled());
        QVERIFY(!category.isDebugEnabled());
        updateVlcQtMessageHandler(true);
        QVERIFY(category.isInfoEnabled());
        QVERIFY(!category.isDebugEnabled());
        QVERIFY(!application.isInfoEnabled());
        QVERIFY(!application.isDebugEnabled());
        qCInfo(category, "category-info");
        qCDebug(category, "category-debug");
        QVERIFY(contains(QStringLiteral("category-info"), VLC_MSG_INFO));
        QCOMPARE(matching(QStringLiteral("category-debug")), 0);
        updateVlcQtMessageHandler(false);
        QVERIFY(!category.isInfoEnabled());
        QVERIFY(!category.isDebugEnabled());
    }
    void testConcurrentRouting()
    {
        std::thread worker([] {
            for (int i = 0; i < 64; ++i)
                QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO, "qt.internal.threadtest").warning("thread-message-%d", i);
        });
        for (int i = 0; i < 16; ++i) updateVlcQtMessageHandler(i % 2);
        worker.join();
        QCOMPARE(matching(QStringLiteral("thread-message-")), 64);
    }
    void testStartupPreference()
    {
        cleanupVlcQtMessageHandler(); m_registered = false;
        auto *object = VLC_OBJECT(m_instance->p_libvlc_int);
        var_SetBool(object, "qt-verbose", true);
        setupVlcQtMessageHandler(object); m_registered = true;
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO, "qt.internal.test").warning("startup-preference");
        QVERIFY(contains(QStringLiteral("startup-preference"), VLC_MSG_WARN));
    }
    void testPreviousHandlerRestored()
    {
        updateVlcQtMessageHandler(true);
        cleanupVlcQtMessageHandler(); m_registered = false;
        QVERIFY(qInstallMessageHandler(fallback) == fallback);
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO, "qt.internal.test").warning("restored-handler");
        QVERIFY(m_fallback.contains(QStringLiteral("restored-handler")));
        QVERIFY(!contains(QStringLiteral("restored-handler"), VLC_MSG_WARN));
    }
};

TestVlcQtMessageHandler *TestVlcQtMessageHandler::s_test = nullptr;
QTEST_GUILESS_MAIN(TestVlcQtMessageHandler)
#include "test_vlcqtmessagehandler.moc"
