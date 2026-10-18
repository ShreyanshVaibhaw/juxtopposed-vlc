// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include "util/file_log_preferences.hpp"

class TestFileLogPreferences : public QObject
{
    Q_OBJECT
private slots:
    void savedLevel_data()
    {
        QTest::addColumn<QString>("level");
        QTest::addColumn<bool>("enabled");
        QTest::addColumn<qint64>("fileVerbosity");
        QTest::addColumn<qint64>("verbosity");
        QTest::addColumn<QString>("selection");
        QTest::newRow("new-user") << QString() << false << qint64(-1) << qint64(0) << QString("errors");
        QTest::newRow("new-user-debug-console") << QString() << false << qint64(-1) << qint64(2) << QString("errors");
        QTest::newRow("legacy-lowest") << QString() << true << qint64(-1) << qint64(0) << QString("native-info-errors");
        QTest::newRow("legacy-warning") << QString() << true << qint64(-1) << qint64(1) << QString("info");
        QTest::newRow("legacy-debug") << QString() << true << qint64(-1) << qint64(2) << QString("debug");
        QTest::newRow("legacy-silent") << QString() << true << qint64(-1) << qint64(-1) << QString("silent");
        QTest::newRow("file-overrides-debug") << QString() << true << qint64(0) << qint64(2) << QString("native-info-errors");
        QTest::newRow("file-overrides-lowest") << QString() << true << qint64(1) << qint64(0) << QString("info");
        QTest::newRow("file-debug") << QString() << true << qint64(3) << qint64(0) << QString("debug");
        QTest::newRow("file-silent") << QString() << true << qint64(-2) << qint64(3) << QString("silent");
        QTest::newRow("large-native-verbosity") << QString() << true << qint64(999) << qint64(0) << QString("debug");
        QTest::newRow("saved-disabled-log") << QString("debug") << false << qint64(-1) << qint64(0) << QString("debug");
        QTest::newRow("strict-error") << QString("errors") << true << qint64(3) << qint64(2) << QString("errors");
        QTest::newRow("strict-warning") << QString("warnings") << true << qint64(-1) << qint64(2) << QString("warnings");
        QTest::newRow("strict-info") << QString("info") << true << qint64(0) << qint64(0) << QString("info");
        QTest::newRow("strict-debug") << QString("debug") << true << qint64(-1) << qint64(-1) << QString("debug");
        QTest::newRow("strict-silent") << QString("silent") << true << qint64(3) << qint64(2) << QString("silent");
        QTest::newRow("preserve-unrecognized-saved") << QString("external-value") << true << qint64(-1) << qint64(0) << QString("external-value");
    }
    void savedLevel()
    {
        QFETCH(QString, level);
        QFETCH(bool, enabled);
        QFETCH(qint64, fileVerbosity);
        QFETCH(qint64, verbosity);
        QFETCH(QString, selection);
        QCOMPARE(vlc::filelog::selection(level, enabled, fileVerbosity, verbosity), selection);
    }
};
QTEST_APPLESS_MAIN(TestFileLogPreferences)
#include "test_file_log_preferences.moc"
