// SPDX-License-Identifier: GPL-2.0-or-later
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0603
# undef _WIN32_WINNT
# define _WIN32_WINNT 0x0603
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <QTest>
#include "maininterface/compositor_dcomp_backdrop.hpp"
#include "maininterface/compositor_dcomp_background.hpp"

using Microsoft::WRL::ComPtr;

class TestDCompBackdrop : public QObject
{
    Q_OBJECT
private slots:
    void opaqueResizeBackground_data()
    {
        QTest::addColumn<bool>("ownDevice");
        QTest::newRow("supplied-device") << false;
        QTest::newRow("independent-device") << true;
    }
    void opaqueResizeBackground()
    {
        QFETCH(bool, ownDevice);
        QVERIFY(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
        struct ComGuard { ~ComGuard() { CoUninitialize(); } } comGuard;
        ComPtr<ID3D11Device> gpu;
        QCOMPARE(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            &gpu, nullptr, nullptr), S_OK);
        ComPtr<IDXGIDevice> dxgi;
        QCOMPARE(gpu.As(&dxgi), S_OK);
        ComPtr<IDCompositionDevice> device;
        QCOMPARE(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&device)), S_OK);
        vlc::DCompOpaqueBackground background;
        QCOMPARE(background.initialize(device.Get(), ownDevice ? nullptr : gpu.Get()), S_OK);
        QVERIFY(background.visual());
        const HWND hwnd = CreateWindowExW(0x00200000L /* WS_EX_NOREDIRECTIONBITMAP */, L"STATIC", L"Opaque resize test",
            WS_POPUP, 0, 0, 960, 540, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd);
        struct WindowGuard { HWND hwnd; ~WindowGuard() { DestroyWindow(hwnd); } } guard{hwnd};
        ComPtr<IDCompositionTarget> target;
        QCOMPARE(device->CreateTargetForHwnd(hwnd, TRUE, &target), S_OK);
        ComPtr<IDCompositionVisual> root, ui, video;
        QCOMPARE(device->CreateVisual(&root), S_OK);
        QCOMPARE(device->CreateVisual(&ui), S_OK);
        QCOMPARE(device->CreateVisual(&video), S_OK);
        QCOMPARE(root->AddVisual(ui.Get(), FALSE, nullptr), S_OK);
        // Reject the exact null-reference call that put black above the UI.
        QCOMPARE(background.attachBelow(root.Get(), nullptr), E_INVALIDARG);
        QCOMPARE(background.attachBelow(nullptr, ui.Get()), E_INVALIDARG);
        QCOMPARE(background.attachBelow(root.Get(), ui.Get()), S_OK);
        QCOMPARE(root->AddVisual(video.Get(), FALSE, ui.Get()), S_OK);
        QCOMPARE(target->SetRoot(root.Get()), S_OK);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        auto *content = background.content();
        QVERIFY(content);
        DXGI_SWAP_CHAIN_DESC1 desc{};
        QCOMPARE(content->GetDesc1(&desc), S_OK);
        QCOMPARE(desc.Width, 1u); QCOMPARE(desc.Height, 1u);
        QCOMPARE(desc.AlphaMode, DXGI_ALPHA_MODE_IGNORE);
        for (const auto size : {QSize(960, 540), QSize(961, 541), QSize(1921, 1081),
                               QSize(3840, 2160), QSize(621, 421), QSize(2560, 1392)})
        {
            QVERIFY(SetWindowPos(hwnd, nullptr, 0, 0, size.width(), size.height(),
                SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER));
            RECT client{};
            QVERIFY(GetClientRect(hwnd, &client));
            const QSize nativeSize(client.right - client.left, client.bottom - client.top);
            QCOMPARE(nativeSize, size);
            QCOMPARE(background.setSize(nativeSize), S_OK);
            QCOMPARE(background.setSize(size), S_FALSE);
            QCOMPARE(device->Commit(), S_OK);
            QCOMPARE(background.content(), content);
            QCOMPARE(content->GetDesc1(&desc), S_OK);
            QCOMPARE(desc.Width, 1u); QCOMPARE(desc.Height, 1u);
        }
        // Both buffers stay opaque black; no new frame or allocation is needed
        // when the independently sized video/UI expose a new row or column.
        ComPtr<ID3D11Device> drawingDevice;
        QCOMPARE(content->GetDevice(IID_PPV_ARGS(&drawingDevice)), S_OK);
        ComPtr<ID3D11DeviceContext> context;
        drawingDevice->GetImmediateContext(&context);
        for (UINT i = 0; i < desc.BufferCount; ++i)
        {
            ComPtr<ID3D11Texture2D> pixel;
            QCOMPARE(content->GetBuffer(i, IID_PPV_ARGS(&pixel)), S_OK);
            D3D11_TEXTURE2D_DESC texture{};
            pixel->GetDesc(&texture);
            texture.Usage = D3D11_USAGE_STAGING;
            texture.BindFlags = 0; texture.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            texture.MiscFlags = 0;
            ComPtr<ID3D11Texture2D> readback;
            QCOMPARE(drawingDevice->CreateTexture2D(&texture, nullptr, &readback), S_OK);
            context->CopyResource(readback.Get(), pixel.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            QCOMPARE(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped), S_OK);
            const auto rgba = *static_cast<const quint32 *>(mapped.pData);
            context->Unmap(readback.Get(), 0);
            QCOMPARE(rgba, 0xff000000u);
        }
        QCOMPARE(device->WaitForCommitCompletion(), S_OK);
        BOOL valid = FALSE;
        QCOMPARE(device->CheckDeviceState(&valid), S_OK);
        QVERIFY(valid);
        QCOMPARE(target->SetRoot(nullptr), S_OK);
        QCOMPARE(device->Commit(), S_OK);
        QCOMPARE(device->WaitForCommitCompletion(), S_OK);
    }
    void lifecycle()
    {
        QVERIFY(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
        struct ComGuard { ~ComGuard() { CoUninitialize(); } } comGuard;
        ComPtr<ID3D11Device> gpu;
        ComPtr<ID3D11DeviceContext> context;
        QCOMPARE(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                  D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                  D3D11_SDK_VERSION, &gpu, nullptr, &context), S_OK);
        ComPtr<IDXGIDevice> dxgi;
        QCOMPARE(gpu.As(&dxgi), S_OK);
        ComPtr<IDXGIAdapter> adapter;
        QCOMPARE(dxgi->GetAdapter(&adapter), S_OK);
        ComPtr<IDXGIFactory2> factory;
        QCOMPARE(adapter->GetParent(IID_PPV_ARGS(&factory)), S_OK);
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = 256; desc.Height = 128;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        ComPtr<IDXGISwapChain1> first, second;
        QCOMPARE(factory->CreateSwapChainForComposition(gpu.Get(), &desc, nullptr, &first), S_OK);
        desc.Width = 512;
        QCOMPARE(factory->CreateSwapChainForComposition(gpu.Get(), &desc, nullptr, &second), S_OK);
        ComPtr<IDCompositionDevice> device;
        QCOMPARE(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&device)), S_OK);
        const HWND hwnd = CreateWindowExW(0, L"STATIC", L"Backdrop test",
                                          WS_POPUP, 0, 0, 540, 140, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd);
        struct WindowGuard { HWND hwnd; ~WindowGuard() { DestroyWindow(hwnd); } } guard{hwnd};
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        ComPtr<IDCompositionTarget> target;
        QCOMPARE(device->CreateTargetForHwnd(hwnd, TRUE, &target), S_OK);
        ComPtr<IDCompositionVisual> root, original, ui;
        QCOMPARE(device->CreateVisual(&root), S_OK);
        QCOMPARE(device->CreateVisual(&original), S_OK);
        QCOMPARE(device->CreateVisual(&ui), S_OK);
        QCOMPARE(root->AddVisual(original.Get(), FALSE, nullptr), S_OK);
        QCOMPARE(root->AddVisual(ui.Get(), TRUE, original.Get()), S_OK);
        QCOMPARE(target->SetRoot(root.Get()), S_OK);

        auto backdrop = std::make_shared<vlc::DCompVideoBackdrop>(device.Get(), root.Get(), ui.Get());
        VideoBackdropState state{1, {{1, QRectF(40, 20, 120, 80), 25, 10}}};
        QVERIFY(SUCCEEDED(backdrop->setRegions(state)));
        QVERIFY(SUCCEEDED(backdrop->setEnabled(true)));
        ComPtr<IDCompositionVisual> proxy;
        proxy.Attach(vlc::makeVideoBackdropProxy(original.Get(), backdrop));
        ComPtr<IUnknown> identity;
        QCOMPARE(proxy.As(&identity), S_OK);
        QCOMPARE(identity.Get(), static_cast<IUnknown *>(proxy.Get()));
        ComPtr<IDCompositionVisual2> unsupported;
        QCOMPARE(proxy.As(&unsupported), E_NOINTERFACE);
        QCOMPARE(proxy->SetContent(first.Get()), S_OK);
        QCOMPARE(backdrop->setRegions(state), S_FALSE);
        QCOMPARE(backdrop->setPosition(QPointF(10, 15)), S_OK);
        state.revision = 2;
        state.regions[0].rect = QRectF(60, 20, 110, 90);
        QCOMPARE(backdrop->setRegions(state), S_OK);
        const VideoBackdropState stale{1, {}};
        QCOMPARE(backdrop->setRegions(stale), S_FALSE);
        QCOMPARE(proxy->SetContent(second.Get()), S_OK);
        QVERIFY(FAILED(proxy->SetContent(identity.Get())));
        QCOMPARE(proxy->SetContent(nullptr), S_OK);
        QCOMPARE(proxy->SetContent(first.Get()), S_OK);
        state.revision = 3;
        state.regions.append({2, QRectF(180, 10, 70, 110), 12, 6});
        QCOMPARE(backdrop->setRegions(state), S_OK);
        state.revision = 4;
        state.regions.removeFirst();
        QCOMPARE(backdrop->setRegions(state), S_OK);
        QVERIFY(SUCCEEDED(backdrop->setEnabled(false)));
        QVERIFY(SUCCEEDED(backdrop->setEnabled(true)));
        QCOMPARE(proxy->SetContent(second.Get()), S_OK);
        QCOMPARE(device->Commit(), S_OK);
        QCOMPARE(device->WaitForCommitCompletion(), S_OK);
        BOOL valid = FALSE;
        QCOMPARE(device->CheckDeviceState(&valid), S_OK);
        QVERIFY(valid);
        // A renderer may release its raw visual handle after the UI is closed.
        backdrop->close();
        QCOMPARE(proxy->SetContent(first.Get()), S_OK);
        state.revision = 5;
        state.regions.clear();
        QCOMPARE(backdrop->setRegions(state), S_FALSE);
        QCOMPARE(target->SetRoot(nullptr), S_OK);
        QCOMPARE(device->Commit(), S_OK);
    }
};

QTEST_GUILESS_MAIN(TestDCompBackdrop)
#include "test_dcomp_backdrop.moc"
