// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include "dialogs/systray/notification_policy.hpp"

class TestTrayNotificationPolicy : public QObject
{
    Q_OBJECT
private slots:
    void delivery_data()
    {
        QTest::addColumn<int>("policy");
        QTest::addColumn<QWindow::Visibility>("visibility");
        QTest::addColumn<bool>("expected");
        const QWindow::Visibility states[] = {QWindow::Hidden, QWindow::AutomaticVisibility,
            QWindow::Windowed, QWindow::Minimized, QWindow::Maximized, QWindow::FullScreen};
        const bool expected[3][6] = {
            {false, false, false, false, false, false},
            {true, false, false, true, false, false},
            {true, true, true, true, true, true}
        };
        for (int policy = 0; policy < 3; ++policy)
            for (int state = 0; state < 6; ++state)
                QTest::newRow(qPrintable(QString("mode-%1-window-%2").arg(policy).arg(state)))
                    << policy << states[state] << expected[policy][state];
        for (const auto policy : {-1, 3})
            for (const auto state : states)
                QTest::newRow(qPrintable(QString("unknown-%1-window-%2").arg(policy).arg(state)))
                    << policy << state << false;
    }
    void delivery()
    {
        QFETCH(int, policy);
        QFETCH(QWindow::Visibility, visibility);
        QFETCH(bool, expected);
        QCOMPARE(vlc::systray::shouldNotify(policy, visibility), expected);
    }
};
QTEST_APPLESS_MAIN(TestTrayNotificationPolicy)
#include "test_tray_notification_policy.moc"
