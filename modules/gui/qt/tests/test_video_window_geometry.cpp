// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include "util/video_window_geometry.hpp"

class TestVideoWindowGeometry : public QObject
{
    Q_OBJECT
private slots:
    void fit_data()
    {
        QTest::addColumn<QSize>("video");
        QTest::addColumn<QSize>("chrome");
        QTest::addColumn<QSize>("screen");
        QTest::addColumn<QSize>("minimum");
        QTest::addColumn<QSize>("maximum");
        QTest::addColumn<QSize>("expected");
        const QSize screen(1600, 1000), minimum(640, 360), maximum(16777215, 16777215), chrome(20, 94);
        QTest::newRow("native-pixels-with-chrome") << QSize(960, 540) << chrome << screen << minimum << maximum << QSize(980, 634);
        QTest::newRow("custom-toolbar") << QSize(960, 540) << QSize(20, 148) << screen << minimum << maximum << QSize(980, 688);
        QTest::newRow("scaled-chrome") << QSize(960, 540) << QSize(30, 141) << screen << minimum << maximum << QSize(990, 681);
        QTest::newRow("compact-native-pixels") << QSize(320, 180) << QSize(0, 0) << screen << QSize(128, 16) << maximum << QSize(320, 180);
        QTest::newRow("tiny-video-keeps-minimum") << QSize(320, 180) << chrome << screen << minimum << maximum << minimum;
        QTest::newRow("wide-video-fits-screen") << QSize(3200, 1600) << chrome << screen << minimum << maximum << QSize(1600, 884);
        QTest::newRow("portrait-fits-height") << QSize(900, 1800) << chrome << screen << minimum << maximum << QSize(640, 1000);
        QTest::newRow("custom-maximum") << QSize(2000, 1000) << chrome << screen << minimum << QSize(1000, 700) << QSize(1000, 584);
        QTest::newRow("small-screen-keeps-minimum") << QSize(960, 540) << chrome << QSize(500, 300) << minimum << maximum << minimum;
        QTest::newRow("missing-video") << QSize(0, 540) << chrome << screen << minimum << maximum << QSize();
        QTest::newRow("missing-screen") << QSize(960, 540) << chrome << QSize(0, 0) << minimum << maximum << QSize();
    }
    void fit()
    {
        QFETCH(QSize, video); QFETCH(QSize, chrome); QFETCH(QSize, screen);
        QFETCH(QSize, minimum); QFETCH(QSize, maximum); QFETCH(QSize, expected);
        QCOMPARE(vlc::video::fitWindowSize(video, chrome, screen, minimum, maximum), expected);
    }
};
QTEST_APPLESS_MAIN(TestVideoWindowGeometry)
#include "test_video_window_geometry.moc"
