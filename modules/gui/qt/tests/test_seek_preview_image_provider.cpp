// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"
#include <QTest>
#include <QBuffer>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickView>
#include <QQuickItem>
#include "../plugins.hpp"
#include "../util/seek_preview_image_provider.hpp"
#include "../util/seek_preview_cache.hpp"

class SeekPreviewImageProviderTest : public QObject
{
    Q_OBJECT
    QByteArray m_png;
private slots:
    void initTestCase()
    {
        QImage image(16, 9, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        image.setPixelColor(15, 8, Qt::blue);
        QBuffer buffer(&m_png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
    }

    void preservesPixelsAndIntrinsicSize()
    {
        SeekPreviewImageProvider provider;
        const auto url = SeekPreviewImageProvider::url(m_png);
        QSize size;
        const auto image = provider.requestImage(url.path().mid(1), &size, {});
        QCOMPARE(size, QSize(16, 9));
        QCOMPARE(image.pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(image.pixelColor(15, 8), QColor(Qt::blue));
    }

    void scalesWithoutChangingAspectRatio()
    {
        SeekPreviewImageProvider provider;
        QSize size;
        const auto image = provider.requestImage(SeekPreviewImageProvider::url(m_png).path().mid(1), &size, QSize(8, 8));
        QCOMPARE(size, QSize(16, 9));
        QCOMPARE(image.size(), QSize(8, 4));
    }

    void rejectsInvalidImages()
    {
        SeekPreviewImageProvider provider;
        QVERIFY(provider.requestImage(QStringLiteral("not-a-png"), nullptr, {}).isNull());
    }

    void roundedFrame_data()
    {
        QTest::addColumn<QSize>("videoSize");
        QTest::addColumn<int>("density");
        for (const auto density : {1, 2}) {
            QTest::newRow(qPrintable(QString("wide-%1x").arg(density))) << QSize(160, 90) << density;
            QTest::newRow(qPrintable(QString("portrait-%1x").arg(density))) << QSize(80, 160) << density;
            QTest::newRow(qPrintable(QString("cinema-%1x").arg(density))) << QSize(160, 68) << density;
        }
    }

    void roundedFrame()
    {
        QFETCH(QSize, videoSize); QFETCH(int, density);
        QImage video(videoSize, QImage::Format_RGB32); video.fill(Qt::red);
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(video.save(&buffer, "PNG"));
        SeekPreviewImageProvider provider;
        const auto id = SeekPreviewImageProvider::url(png).path().mid(1) + "?rounded=1&radius=4&width=252";
        QSize intrinsic;
        const auto frame = provider.requestImage(id, &intrinsic, QSize(252, 142) * density);
        QCOMPARE(frame.size(), QSize(252, 142) * density);
        QCOMPARE(intrinsic, frame.size());
        QCOMPARE(frame.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(frame.pixelColor(frame.width()-1, 0).alpha(), 0);
        QCOMPARE(frame.pixelColor(0, frame.height()-1).alpha(), 0);
        QCOMPARE(frame.pixelColor(frame.width()-1, frame.height()-1).alpha(), 0);
        QCOMPARE(frame.pixelColor(126*density, 71*density), QColor(Qt::red));
        bool antialiased = false;
        for (int y=0; y<4*density; ++y) for (int x=0; x<4*density; ++x) {
            const auto alpha = frame.pixelColor(x,y).alpha();
            antialiased |= alpha > 0 && alpha < 255;
        }
        QVERIFY(antialiased);
        if (videoSize.height() > videoSize.width()) {
            QCOMPARE(frame.pixelColor(2*density,71*density), QColor(Qt::black));
            QCOMPARE(frame.pixelColor(89*density,71*density), QColor(Qt::black));
            QVERIFY(frame.pixelColor(92*density,71*density).red() > 240);
        } else if (videoSize.width() * 9 > videoSize.height() * 16) {
            QCOMPARE(frame.pixelColor(126*density,2*density), QColor(Qt::black));
            QCOMPARE(frame.pixelColor(126*density,16*density), QColor(Qt::black));
            QVERIFY(frame.pixelColor(126*density,19*density).red() > 240);
        } else {
            QCOMPARE(frame.pixelColor(126*density,0), QColor(Qt::red));
            QCOMPARE(frame.pixelColor(2*density,71*density), QColor(Qt::red));
        }
    }

    void roundedImageRendersInSoftware()
    {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        QQuickView view;
        view.engine()->addImageProvider(QStringLiteral("seekpreview"), new SeekPreviewImageProvider());
        QQmlComponent component(view.engine());
        component.setData("import QtQuick\nimport \".\" as Redesign\n"
            "Rectangle { width: 300; height: 190; color: \"#506070\"\n"
            "Redesign.RedesignSeekThumbnail { objectName: \"thumbnail\"; x: 20; y: 20; width: 252; height: 142 }\n}",
            QUrl::fromLocalFile(QStringLiteral(TOP_SRCDIR "/modules/gui/qt/player/qml/redesign/")));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        auto* root = qobject_cast<QQuickItem*>(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        view.setContent(QUrl(), &component, root); view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto* image = root->findChild<QObject*>("thumbnail"); QVERIFY(image);
        image->setProperty("frameSource", SeekPreviewImageProvider::url(m_png));
        QTRY_COMPARE(image->property("status").toInt(), 1);
        QTest::qWait(50);
        const auto pixels = view.grabWindow(); QVERIFY(!pixels.isNull());
        const auto density = pixels.width() / 300.;
        QCOMPARE(pixels.pixelColor(qRound(20*density),qRound(20*density)), QColor("#506070"));
        QCOMPARE(pixels.pixelColor(qRound(145*density),qRound(90*density)), QColor(Qt::red));
        const auto output = qEnvironmentVariable("VLC_TEST_THUMBNAIL_OUTPUT");
        if (!output.isEmpty()) QVERIFY(pixels.save(output));
        image->setProperty("frameSource", QUrl());
        QTRY_COMPARE(image->property("status").toInt(), 0);
    }

    void previewCardDoesNotAddABlackOutline()
    {
        QQuickView view;
        view.engine()->addImageProvider(QStringLiteral("seekpreview"), new SeekPreviewImageProvider());
        QQmlComponent component(view.engine());
        component.setData("import QtQuick\nimport \".\" as Redesign\n"
            "Rectangle { width: 400; height: 400; color: \"#506070\"\n"
            "Redesign.RedesignSeekBar { objectName: \"bar\"; x: 20; y: 250; width: 300; height: 26; value: 0.5; previewEnabled: true; previewImagePosition: 0.5 }\n}",
            QUrl::fromLocalFile(QStringLiteral(TOP_SRCDIR "/modules/gui/qt/player/qml/redesign/")));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        auto* root = qobject_cast<QQuickItem*>(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        view.setContent(QUrl(), &component, root); view.show(); view.requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto* bar = root->findChild<QQuickItem*>("bar"); QVERIFY(bar);
        bar->forceActiveFocus(Qt::TabFocusReason);
        bar->setProperty("previewImage", SeekPreviewImageProvider::url(m_png));
        auto* image = root->findChild<QQuickItem*>("seekThumbnailImage"); QVERIFY(image);
        QTRY_COMPARE(image->property("status").toInt(), 1);
        auto* popup = bar->findChild<QObject*>("seekThumbnailPopup"); QVERIFY(popup);
        QTRY_VERIFY(popup->property("visible").toBool());
        QTest::qWait(50);
        const auto pixels = view.grabWindow(); QVERIFY(!pixels.isNull());
        const auto density = pixels.width() / 400.;
        const auto origin = image->mapToScene(QPointF(0,0)) * density;
        const int left = qRound(origin.x()), top = qRound(origin.y());
        const auto background = pixels.pixelColor(left - 1, top + qRound(10*density));
        const QSize frameSize(qRound(252*density), qRound(142*density));
        SeekPreviewImageProvider provider;
        const auto frame = provider.requestImage(SeekPreviewImageProvider::url(m_png).path().mid(1) + "?rounded=1&radius=4&width=252", nullptr, frameSize);
        QImage expected(frameSize, QImage::Format_ARGB32_Premultiplied); expected.fill(background);
        { QPainter painter(&expected); painter.drawImage(0,0,frame); }
        for (int y=0; y<4*density; ++y) for (int x=0; x<4*density; ++x) {
            const auto actual = pixels.pixelColor(left+x,top+y), reference = expected.pixelColor(x,y);
            QVERIFY2(std::abs(actual.red()-reference.red()) <= 1 && std::abs(actual.green()-reference.green()) <= 1
                && std::abs(actual.blue()-reference.blue()) <= 1, qPrintable(QString("Incorrect corner composition at %1,%2: %3 instead of %4").arg(x).arg(y).arg(actual.name()).arg(reference.name())));
        }
        const auto output = qEnvironmentVariable("VLC_TEST_PREVIEW_CARD_OUTPUT");
        if (!output.isEmpty()) QVERIFY(pixels.save(output));
    }

    void samplesCoverTheTimelineWithinBounds()
    {
        QVERIFY(SeekPreviewCache::samplesFor(0).isEmpty());
        QVERIFY(SeekPreviewCache::samplesFor(-1).isEmpty());
        QCOMPARE(SeekPreviewCache::samplesFor(1), QVector<qint64>{0});
        for (qint64 length : {qint64(500001), qint64(30000000), qint64(10800000000)}) {
            const auto points = SeekPreviewCache::samplesFor(length);
            QVERIFY(points.size() <= SeekPreviewCache::MaximumSamples);
            QCOMPARE(points.front(), qint64(0));
            QCOMPARE(points[1], (length - 1) / SeekPreviewCache::Bucket * SeekPreviewCache::Bucket);
            auto sorted = points;
            std::sort(sorted.begin(), sorted.end());
            QVERIFY(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
            for (const auto time : points) {
                QVERIFY(time >= 0 && time < length);
                QCOMPARE(time % SeekPreviewCache::Bucket, qint64(0));
            }
            if (points.size() > 2)
                QVERIFY(std::abs(points[2] - length / 2) < length / 10 + SeekPreviewCache::Bucket);
        }
        QCOMPARE(SeekPreviewCache::samplesFor(10800000000).size(), SeekPreviewCache::MaximumSamples);
    }

    void cacheUsesDecodedTimesAndDoesNotStoreFailures()
    {
        SeekPreviewCache cache;
        QVERIFY(!cache.insert(0, QUrl(), 0, true));
        QVERIFY(!cache.nearest(0));
        const QUrl first(QStringLiteral("image://seekpreview/first"));
        const QUrl second(QStringLiteral("image://seekpreview/second"));
        QVERIFY(cache.insert(0, first, 2000000, true));
        QVERIFY(cache.insert(10000000, second, 8000000, false));
        QCOMPARE(cache.nearest(3000000)->image, first);
        QCOMPARE(cache.nearest(7000000)->image, second);
        QCOMPARE(cache.nearest(7000000)->time, qint64(8000000));
        QVERIFY(!cache.exact(0)); // Coarse seeks still need a precise refinement.
        QVERIFY(cache.exact(10000000));
        QVERIFY(!cache.insert(10000000, QUrl(), 0, false));
        QCOMPARE(cache.exact(10000000)->image, second);
    }

    void preciseEvictionPreservesCoarseCoverage()
    {
        SeekPreviewCache cache;
        const QUrl coarse(QStringLiteral("image://seekpreview/coarse"));
        QVERIFY(cache.insert(0, coarse, 0, true));
        for (int i = 1; i <= 100; ++i)
            QVERIFY(cache.insert(i * SeekPreviewCache::Bucket,
                QUrl(QStringLiteral("image://seekpreview/%1").arg(i)), i * SeekPreviewCache::Bucket, false));
        QCOMPARE(cache.count(true), 1);
        QCOMPARE(cache.count(false), 32);
        QVERIFY(!cache.exact(SeekPreviewCache::Bucket));
        QCOMPARE(cache.nearest(0)->image, coarse);
        cache.clear();
        QCOMPARE(cache.count(true), 0);
        QCOMPARE(cache.count(false), 0);
        QVERIFY(!cache.nearest(0));
    }

    void loadsAndClearsThroughQml()
    {
        QQmlEngine engine;
        engine.addImageProvider(QStringLiteral("seekpreview"), new SeekPreviewImageProvider());
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nImage { cache: false; property bool ready: status === Image.Ready }", QUrl());
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QScopedPointer<QObject> image(component.create());
        QVERIFY(image);
        QVERIFY(image->setProperty("source", SeekPreviewImageProvider::url(m_png)));
        QTRY_VERIFY(image->property("ready").toBool());
        QCOMPARE(image->property("implicitWidth").toInt(), 16);
        QCOMPARE(image->property("implicitHeight").toInt(), 9);
        QVERIFY(image->setProperty("source", QUrl()));
        QTRY_COMPARE(image->property("status").toInt(), 0);
    }
};

QTEST_MAIN(SeekPreviewImageProviderTest)
#include "test_seek_preview_image_provider.moc"
