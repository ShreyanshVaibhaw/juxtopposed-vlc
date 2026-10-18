// SPDX-License-Identifier: GPL-2.0-or-later
#include <QGuiApplication>
#include <QScreen>
#include <QTest>
#include <QtPlugin>
#include <utility>
#include "maininterface/window_fullscreen.hpp"
#include "maininterface/window_csd_geometry.hpp"

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

// Use the same non-client calculation as VLC's custom window decorations.
// The frame-only cases cannot detect content clipped by incorrect margins.
class ClientDecoration final : public QAbstractNativeEventFilter
{
public:
    ClientDecoration(QWindow *window, bool platformHandlesResize)
        : m_window(window), m_resize(platformHandlesResize),
          m_handle(reinterpret_cast<HWND>(window->winId()))
    {
        QCoreApplication::instance()->installNativeEventFilter(this);
        SetWindowPos(m_handle, nullptr, 0, 0, 0, 0,
            SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    }
    ~ClientDecoration() override
    {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
    bool nativeEventFilter(const QByteArray&, void *message, qintptr *result) override
    {
        const auto *event = static_cast<MSG *>(message);
        if (event->hwnd != m_handle || event->message != WM_NCCALCSIZE)
            return false;
        *result = 0;
        if (event->wParam)
        {
            auto *rect = &reinterpret_cast<NCCALCSIZE_PARAMS *>(event->lParam)->rgrc[0];
            const auto user32 = GetModuleHandleW(L"user32.dll");
            const auto dpiForWindow = reinterpret_cast<UINT (WINAPI *)(HWND)>(
                GetProcAddress(user32, "GetDpiForWindow"));
            const auto metrics = reinterpret_cast<int (WINAPI *)(int, UINT)>(
                GetProcAddress(user32, "GetSystemMetricsForDpi"));
            const auto metric = [&](int index) {
                return metrics && dpiForWindow ? metrics(index, dpiForWindow(m_handle)) : GetSystemMetrics(index);
            };
            const bool adjusted = adjustCSDClientRect(m_window, m_resize,
                metric(SM_CXSIZEFRAME) + metric(SM_CXPADDEDBORDER),
                metric(SM_CYSIZEFRAME) + metric(SM_CXPADDEDBORDER), rect);
            *result = adjusted ? 0 : WVR_REDRAW;
        }
        return true;
    }
    QRect client() const
    {
        RECT rect = {};
        GetClientRect(m_handle, &rect);
        MapWindowPoints(m_handle, nullptr, reinterpret_cast<POINT *>(&rect), 2);
        return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
    }
    QRect monitor(bool workArea) const
    {
        MONITORINFO info = {};
        info.cbSize = sizeof(info);
        GetMonitorInfoW(MonitorFromWindow(m_handle, MONITOR_DEFAULTTONEAREST), &info);
        const auto rect = workArea ? info.rcWork : info.rcMonitor;
        return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
    }
private:
    QWindow *m_window;
    bool m_resize;
    HWND m_handle;
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
    static void open(QWindow& window, int screenIndex, Qt::WindowFlags flags, bool visible = true)
    {
        auto *screen = QGuiApplication::screens().at(screenIndex);
        window.setScreen(screen);
        window.setFlags(flags);
        window.setGeometry(QRect(screen->availableGeometry().topLeft() + QPoint(80, 80),
                                  QSize(620, 420)));
        if (visible)
            window.show();
        QCoreApplication::processEvents();
    }
private slots:
    void initTestCase()
    {
        if (QGuiApplication::platformName() != QStringLiteral("windows"))
            QSKIP("Requires the native Windows platform plugin");
    }
    void customDecorationRestore_data()
    {
        QTest::addColumn<int>("screenIndex");
        QTest::addColumn<bool>("platformResize");
        for (int i = 0; i < QGuiApplication::screens().size(); ++i)
            for (bool resize : {false, true})
                QTest::newRow(qPrintable(QStringLiteral("screen%1-csd-resize%2").arg(i).arg(resize)))
                    << i << resize;
    }
    void customDecorationRestore()
    {
        QFETCH(int, screenIndex);
        QFETCH(bool, platformResize);
        QWindow window;
        open(window, screenIndex, Qt::Window, false);
        ClientDecoration decoration(&window, platformResize);
        window.show();
        QCoreApplication::processEvents();
        const auto normal = decoration.client();
        const auto setState = [&](Qt::WindowStates states) {
            // WindowStateHolder updates the source counter before the native transition.
            window.setProperty("__windowFullScreen", states.testFlag(Qt::WindowFullScreen) ? 1 : 0);
            setWindowStatesPreservingGeometry(&window, states);
            QCoreApplication::processEvents();
        };
        setState(Qt::WindowMaximized);
        QCOMPARE(decoration.client(), decoration.monitor(true));
        // Compare in Qt's logical units: fractional scale factors can round
        // a physical screen edge to the nearest logical pixel.
        QCOMPARE(window.size(), decoration.client().size() / window.devicePixelRatio());
        const auto maximized = FrameHistory::frame(&window);
        FrameHistory history(&window);
        for (int repeat = 0; repeat < 3; ++repeat)
        {
            setState(Qt::WindowMaximized | Qt::WindowFullScreen);
            QCOMPARE(decoration.client(), decoration.monitor(false));
            const auto fullscreen = FrameHistory::frame(&window);
            history.frames.clear();
            history.collect = true;
            setState(Qt::WindowMaximized);
            history.collect = false;
            QCOMPARE(FrameHistory::frame(&window), maximized);
            QCOMPARE(decoration.client(), decoration.monitor(true));
            QCOMPARE(window.size(), decoration.client().size() / window.devicePixelRatio());
            const auto clientOrigin = decoration.monitor(false).topLeft()
                + (window.position() - window.screen()->geometry().topLeft()) * window.devicePixelRatio();
            QCOMPARE(clientOrigin, decoration.client().topLeft());
            QVERIFY(!history.frames.isEmpty());
            for (const auto& frame : std::as_const(history.frames))
                QVERIFY(frame.width() >= qMin(maximized.width(), fullscreen.width()) - 2 &&
                        frame.height() >= qMin(maximized.height(), fullscreen.height()) - 2);
        }
        setState(Qt::WindowNoState);
        QTRY_COMPARE_WITH_TIMEOUT(decoration.client(), normal, 1500);
        setState(Qt::WindowFullScreen);
        QCOMPARE(decoration.client(), decoration.monitor(false));
        setState(Qt::WindowNoState);
        QTRY_COMPARE_WITH_TIMEOUT(decoration.client(), normal, 1500);
        QCOMPARE(window.size(), normal.size() / window.devicePixelRatio());
        setState(Qt::WindowMaximized);
        setState(Qt::WindowMaximized | Qt::WindowFullScreen);
        setState(Qt::WindowMaximized | Qt::WindowMinimized);
        QVERIFY(IsIconic(reinterpret_cast<HWND>(window.winId())));
        setState(Qt::WindowMaximized);
        QCOMPARE(decoration.client(), decoration.monitor(true));
        QCOMPARE(window.size(), decoration.client().size() / window.devicePixelRatio());
        setState(Qt::WindowNoState);
        QTRY_COMPARE_WITH_TIMEOUT(decoration.client(), normal, 1500);
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
