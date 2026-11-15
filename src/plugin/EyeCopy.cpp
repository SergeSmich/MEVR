// SPDX-License-Identifier: MIT
#include "plugin/EyeCopy.hpp"

#include "core/Log.hpp"
#include "core/d3d11/Blit.hpp"

#include <wrl/client.h>

#include <unordered_map>

namespace mevr::host::eye_copy {
namespace {

using Microsoft::WRL::ComPtr;

// Present thread only.
d3d11::Blit* gBlit = nullptr;  // never destroyed at process exit: no D3D releases then
ComPtr<ID3D11Device> gDevice;
bool gBlitFailed = false;
std::unordered_map<ID3D11Texture2D*, ComPtr<ID3D11RenderTargetView>> gViews;
bool gToldDraw = false;
bool gToldRefused = false;

// The typeless parent of a format (what CopyResource compares), or the format itself.
DXGI_FORMAT Family(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8X8_TYPELESS;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
        return DXGI_FORMAT_R10G10B10A2_TYPELESS;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
        return DXGI_FORMAT_R16G16B16A16_TYPELESS;
    default:
        return f;
    }
}

// The sRGB view the blit draws through (it writes linear light), or UNKNOWN for an image it can't draw into right.
DXGI_FORMAT SrgbView(DXGI_FORMAT image) {
    switch (image) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

ID3D11RenderTargetView* ViewOf(ID3D11Texture2D* image, D3D11_TEXTURE2D_DESC const& desc) {
    auto const it = gViews.find(image);
    if (it != gViews.end()) {
        return it->second.Get();
    }
    D3D11_RENDER_TARGET_VIEW_DESC view{};
    view.Format = SrgbView(desc.Format);
    view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> rtv;
    if (view.Format == DXGI_FORMAT_UNKNOWN || (desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0 ||
        FAILED(gDevice->CreateRenderTargetView(image, &view, &rtv))) {
        rtv.Reset();
    }
    gViews.emplace(image, rtv);  // a failure is remembered as null: not tried again for this image
    return rtv.Get();
}

} // namespace

bool CopyCompatible(DXGI_FORMAT a, DXGI_FORMAT b) {
    return Family(a) == Family(b);
}

bool Copy(ID3D11DeviceContext* context, ID3D11Texture2D* image, ID3D11Texture2D* source, int shiftX) {
    if (context == nullptr || image == nullptr || source == nullptr) {
        return false;
    }
    D3D11_TEXTURE2D_DESC from{};
    D3D11_TEXTURE2D_DESC to{};
    source->GetDesc(&from);
    image->GetDesc(&to);
    if (CopyCompatible(from.Format, to.Format)) {
        return false;  // the renderer's own CopyResource is right
    }
    if (gBlit == nullptr && !gBlitFailed) {
        ComPtr<ID3D11Device> device;
        context->GetDevice(&device);
        auto* blit = new d3d11::Blit;
        if (device != nullptr && blit->Initialize(device.Get())) {
            gBlit = blit;
            gDevice = device;
        } else {
            delete blit;
            gBlitFailed = true;
            log::Error("eye copy: the blit didn't start; an eye whose source is format {} stays as it was",
                static_cast<int>(from.Format));
        }
    }
    ID3D11RenderTargetView* const view = gBlit != nullptr ? ViewOf(image, to) : nullptr;
    if (view == nullptr) {
        if (!gToldRefused) {
            gToldRefused = true;
            log::Warn("eye copy: a source of format {} can't be drawn into an image of format {} (bind {:#x}); left to "
                      "the renderer's copy, which D3D11 drops",
                static_cast<int>(from.Format), static_cast<int>(to.Format), to.BindFlags);
        }
        return false;
    }
    gBlit->Draw(context, source, view, to.Width, to.Height, shiftX);
    if (!gToldDraw) {
        gToldDraw = true;
        log::Info("eye copy: the game's picture ({}x{}, format {}) is drawn into the eye images ({}x{}, format {}) through "
                  "an sRGB view: CopyResource can't copy between the two",
            from.Width, from.Height, static_cast<int>(from.Format), to.Width, to.Height, static_cast<int>(to.Format));
    }
    return true;
}

void Reset() {
    gViews.clear();
}

} // namespace mevr::host::eye_copy
