// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_COMPOSITOR_DCOMP_BACKGROUND_HPP
#define VLC_COMPOSITOR_DCOMP_BACKGROUND_HPP

#include <QSize>
#include <utility>
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl.h>

namespace vlc {

// A permanent opaque pixel underneath the independently resized UI and video.
// Resizing changes only its composition transform, never its buffers or pixels.
class DCompOpaqueBackground
{
public:
    HRESULT initialize(IDCompositionDevice *composition, ID3D11Device *gpu)
    {
        using Microsoft::WRL::ComPtr;
        ComPtr<ID3D11Device> fallback;
        if (!gpu)
        {
            // Keep initialization independent of Qt's render-thread context,
            // including a D3D12 UI. WARP draws these two pixels once; there is
            // no extra rendering loop or full-size buffer.
            static HMODULE library = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!library)
                return HRESULT_FROM_WIN32(GetLastError());
            const auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(
                GetProcAddress(library, "D3D11CreateDevice"));
            if (!create)
                return E_NOINTERFACE;
            const auto result = create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                &fallback, nullptr, nullptr);
            if (FAILED(result))
                return result;
            gpu = fallback.Get();
        }
        ComPtr<IDXGIDevice> dxgi;
        auto result = gpu->QueryInterface(IID_PPV_ARGS(&dxgi));
        if (FAILED(result)) return result;
        ComPtr<IDXGIAdapter> adapter;
        result = dxgi->GetAdapter(&adapter);
        if (FAILED(result)) return result;
        ComPtr<IDXGIFactory2> factory;
        result = adapter->GetParent(IID_PPV_ARGS(&factory));
        if (FAILED(result)) return result;
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = desc.Height = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        ComPtr<IDXGISwapChain1> content;
        result = factory->CreateSwapChainForComposition(gpu, &desc, nullptr, &content);
        if (FAILED(result)) return result;
        ComPtr<ID3D11DeviceContext> context;
        gpu->GetImmediateContext(&context);
        const float black[] = {0, 0, 0, 1};
        for (UINT i = 0; i < desc.BufferCount; ++i)
        {
            ComPtr<ID3D11Texture2D> buffer;
            // D3D11 exposes only buffer zero as a writable render target.
            // Present rotates the next allocation into that slot.
            result = content->GetBuffer(0, IID_PPV_ARGS(&buffer));
            if (FAILED(result)) return result;
            ComPtr<ID3D11RenderTargetView> target;
            result = gpu->CreateRenderTargetView(buffer.Get(), nullptr, &target);
            if (FAILED(result)) return result;
            context->ClearRenderTargetView(target.Get(), black);
            result = content->Present(0, 0);
            if (FAILED(result)) return result;
        }
        ComPtr<IDCompositionVisual> visual;
        result = composition->CreateVisual(&visual);
        if (FAILED(result)) return result;
        result = visual->SetContent(content.Get());
        if (FAILED(result)) return result;
        result = visual->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        if (FAILED(result)) return result;
        result = visual->SetBorderMode(DCOMPOSITION_BORDER_MODE_HARD);
        if (FAILED(result)) return result;
        m_content = std::move(content);
        m_visual = std::move(visual);
        m_size = {};
        return S_OK;
    }

    HRESULT attachBelow(IDCompositionVisual *parent, IDCompositionVisual *foreground)
    {
        if (!m_visual) return E_UNEXPECTED;
        // A null reference reverses AddVisual's insertion semantics: FALSE
        // with no reference means above every sibling, covering the whole UI.
        if (!parent || !foreground) return E_INVALIDARG;
        return parent->AddVisual(m_visual.Get(), FALSE, foreground);
    }

    HRESULT setSize(const QSize &size)
    {
        if (!m_visual || size.isEmpty() || size == m_size)
            return S_FALSE;
        const D2D_MATRIX_3X2_F transform{float(size.width()), 0, 0, float(size.height()), 0, 0};
        const auto result = m_visual->SetTransform(transform);
        if (SUCCEEDED(result)) m_size = size;
        return result;
    }
    IDCompositionVisual *visual() const { return m_visual.Get(); }
    IDXGISwapChain1 *content() const { return m_content.Get(); }

private:
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_content;
    Microsoft::WRL::ComPtr<IDCompositionVisual> m_visual;
    QSize m_size;
};

}
#endif
