// ============================================================================
//  overlay_renderer.h  — DIB + D2D DC render target, отрисовка кадра
// ============================================================================
#pragma once
#include "overlay.h"

class OverlayRenderer {
public:
    OverlayRenderer() = default;
    ~OverlayRenderer() = default;

    OverlayRenderer(const OverlayRenderer&) = delete;
    OverlayRenderer& operator=(const OverlayRenderer&) = delete;

    bool Render(HDC hdc, int screenW, int screenH,
        const std::vector<OverlayImage>& images,
        bool editMode);

    void InvalidateResources();

    bool  EnsureDib(UINT w, UINT h);
    void  ReleaseDib();
    HDC   DibHdc()  const { return m_dib.hdcMem; }
    void* DibBits() const { return m_dib.pBits; }

    static void DrawImageFrameAndHandles(ID2D1RenderTarget* rt,
        ID2D1SolidColorBrush* brush, const OverlayImage& img);

private:
    bool EnsureRenderTarget(UINT w, UINT h);
    bool EnsureDeviceResources(ID2D1RenderTarget* rt,
        const std::vector<OverlayImage>& images);
    bool RenderToHDC(HDC hdc, int w, int h,
        const std::vector<OverlayImage>& images, bool editMode);

    struct D2DCache {
        ComPtr<ID2D1DCRenderTarget>      rt;
        ComPtr<ID2D1SolidColorBrush>     brush;
        std::vector<ComPtr<ID2D1Bitmap>> bmps;
        UINT rtW = 0, rtH = 0;
        bool resourcesReady = false;
    } m_cache;

    struct DibCache {
        HDC     hdcMem = NULL;
        HBITMAP hBitmap = NULL;
        HBITMAP hOldBitmap = NULL;
        void* pBits = NULL;
        UINT    w = 0, h = 0;
    } m_dib;
};