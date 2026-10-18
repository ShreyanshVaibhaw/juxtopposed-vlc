// SPDX-License-Identifier: GPL-2.0-or-later
#include <QGuiApplication>
#include <QScreen>
#include <QTest>
#include <QtPlugin>
#include <utility>
#include "maininterface/window_fullscreen.hpp"

#ifdef QT_STATIC
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

class FrameHistory final : public QAbstractNativeEventFilter
{
public:
    explicit FrameHistory(QWindow *window)
        : m_window(reinterpret_cast<HWND>(window->winId()))
    {
        QCoreApplication::instance()->installNativeEventFilter(this);
    }
    ~FrameHistory() override
    {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
    bool nativeEventFilter(const QByteArray&, void *message, qintptr *) override
    {
        const auto *event = static_cast<MSG *>(message);
        if (collect && event->hwnd == m_window && event->message == WM_WINDOWPOSCHANGED)
            frames.append(frame(m_window));
        return false;
    }
    static QRect frame(HWND window)
    {
        RECT rect = {};
        GetWindowRect(window, &rect);
        return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
    }
    static QRect frame(QWindow *window)
    {
        return frame(reinterpret_cast<HWND>(window->winId()));
    }
    bool collect = false;
    QList<QRect> frames;
private:
    HWND m_window;
};

class TestWindowFullscreen : public QObject
{
    Q_OBJECT
private:
    static void windowData()
    {
        QTest::addColumn<int>("screenIndex");
        QTest::addColumn<Qt::WindowFlags>("flags");
        for (int i = 0; i < QGuiApplication::screens().size(); ++i)
        {
            QTest::newRow(qPrintable(QStringLiteral("screen%1-decorated").arg(i)))
                << i << Qt::WindowFlags(Qt::Window);
            QTest::newRow(qPrintable(QStringLiteral("screen%1-frameless").arg(i)))
                << i << Qt::WindowFlags(Qt::Window | Qt::FramelessWindowHint);
            QTest::newRow(qPrintable(QStringLiteral("screen%1-tool").arg(i)))
                << i << Qt::WindowFlags(Qt::Tool);
        }
    }
    static void open(QWindow& window, int screenIndex, Qt::WindowFlags flags)
    {
        auto *screen = QGuiApplication::screens().at(screenIndex);
        window.setScreen(screen);
        window.setFlags(flags);
        window.setGeometry(QRect(screen->availableGeometry().topLeft() + QPoint(80, 80),
                                  QSize(620, 420)));
        window.show();
        QCoreApplication::processEvents();
    }
private slots:
    void initTestCase()
    {
        if (QGuiApplication::platformName() != QStringLiteral("windows"))
            QSKIP("Requires the native Windows platform plugin");
    }
    void maximizedDoesNotVisitNormalFrame_data() { windowData(); }
    void maximizedDoesNotVisitNormalFrame()
    {
        QFETCH(int, screenIndex);
        QFETCH(Qt::WindowFlags, flags);
        QWindow window;
        open(window, screenIndex, flags);
        const auto normal = FrameHistory::frame(&window);
        setWindowStatesPreservingGeometry(&window, Qt::WindowMaximized);
        QTRY_VERIFY(window.windowStates().testFlag(Qt::WindowMaximized));
        const auto maximized = FrameHistory::frame(&window);
        QVERIFY(maximized.width() > normal.width());
        FrameHistory history(&window);
        for (int repeat = 0; repeat < 3; ++repeat)
        {
            setWindowStatesPreservingGeometry(&window, window.windowStates() | Qt::WindowFullScreen);
            QTRY_VERIFY(window.windowStates().testFlag(Qt::WindowFullScreen));
            const auto fullscreen = FrameHistory::frame(&window);
            history.frames.clear();
            history.collect = true;
            setWindowStatesPreservingGeometry(&window, window.windowStates() & ~Qt::WindowFullScreen);
            QCoreApplication::processEvents();
            history.collect = false;
            QVERIFY(!history.frames.isEmpty());
            for (const auto& frame : std::as_const(history.frames))
            {
                QVERIFY2(frame.width() >= qMin(maximized.width(), fullscreen.width()) - 2 &&
                         frame.height() >= qMin(maximized.height(), fullscreen.height()) - 2,
                         qPrintable(QStringLiteral("Intermediate normal-sized frame: %1,%2 %3x%4")
                             .arg(frame.x()).arg(frame.y()).arg(frame.width()).arg(frame.height())));
            }
            QVERIFY(window.windowStates().testFlag(Qt::WindowMaximized));
            QCOMPARE(FrameHistory::frame(&window), maximized);
        }
        setWindowStatesPreservingGeometry(&window, Qt::WindowNoState);
        QTRY_COMPARE_WITH_TIMEOUT(FrameHistory::frame(&window), normal, 1500);
    }
    void windowedRestore_data() { windowData(); }
    void windowedRestore()
    {
        QFETCH(int, screenIndex);
        QFETCH(Qt::WindowFlags, flags);
        QWindow window;
        open(window, screenIndex, flags);
        const auto normal = FrameHistory::frame(&window);
        setWindowStatesPreservingGeometry(&window, Qt::WindowFullScreen);
        setWindowStatesPreservingGeometry(&window, Qt::WindowNoState);
        QTRY_COMPARE_WITH_TIMEOUT(FrameHistory::frame(&window), normal, 1500);
        QCOMPARE(window.windowStates(), Qt::WindowStates(Qt::WindowNoState));
    }
    void minimizedRestore_data() { windowData(); }
    void minimizedRestore()
    {
        QFETCH(int, screenIndex);
        QFETCH(Qt::WindowFlags, flags);
        QWindow window;
        open(window, screenIndex, flags);
        const auto normal = FrameHistory::frame(&window);
        setWindowStatesPreservingGeometry(&window, Qt::WindowMaximized);
        setWindowStatesPreservingGeometry(&window, Qt::WindowMaximized | Qt::WindowFullScreen);
        setWindowStatesPreservingGeometry(&window, Qt::WindowMaximized | Qt::WindowMinimized);
        QTRY_VERIFY(IsIconic(reinterpret_cast<HWND>(window.winId())));
        setWindowStatesPreservingGeometry(&window, Qt::WindowMaximized);
        setWindowStatesPreservingGeometry(&window, Qt::WindowNoState);
        QTRY_COMPARE_WITH_TIMEOUT(FrameHistory::frame(&window), normal, 1500);
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    TestWindowFullscreen test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_window_fullscreen.moc"
