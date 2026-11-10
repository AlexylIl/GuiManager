// ============================================================================
//  overlay_renderer.cpp
// ============================================================================
#include "overlay.h"
#include "overlay_renderer.h"

void OverlayRenderer::InvalidateResources() {
    m_cache.bmps.clear();
    m_cache.brush.Reset();
    m_cache.rt.Reset();
    m_cache.resourcesReady = false;
    m_cache.rtW = m_cache.rtH = 0;
}

bool OverlayRenderer::EnsureDib(UINT w, UINT h) {
    if (m_dib.hdcMem && m_dib.w == w && m_dib.h == h) return true;

    if (m_dib.hdcMem) {
        if (m_dib.hOldBitmap) SelectObject(m_dib.hdcMem, m_dib.hOldBitmap);
        DeleteDC(m_dib.hdcMem);
        m_dib.hdcMem = NULL;
    }
    if (m_dib.hBitmap) { DeleteObject(m_dib.hBitmap); m_dib.hBitmap = NULL; }
    m_dib.hOldBitmap = NULL; m_dib.pBits = NULL; m_dib.w = m_dib.h = 0;

    HDC hdcScreen = GetDC(NULL);
    if (!hdcScreen) return false;
    m_dib.hdcMem = CreateCompatibleDC(hdcScreen);
    if (!m_dib.hdcMem) { ReleaseDC(NULL, hdcScreen); return false; }

    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)w;
    bmi.bmiHeader.biHeight = -(LONG)h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS,
        &pBits, NULL, 0);
    ReleaseDC(NULL, hdcScreen);
    if (!hBitmap) {
        DeleteDC(m_dib.hdcMem); m_dib.hdcMem = NULL;
        return false;
    }

    m_dib.hBitmap = hBitmap;
    m_dib.hOldBitmap = (HBITMAP)SelectObject(m_dib.hdcMem, hBitmap);
    m_dib.pBits = pBits;
    m_dib.w = w; m_dib.h = h;
    return true;
}

void OverlayRenderer::ReleaseDib() {
    if (m_dib.hdcMem) {
        if (m_dib.hOldBitmap) SelectObject(m_dib.hdcMem, m_dib.hOldBitmap);
        DeleteDC(m_dib.hdcMem); m_dib.hdcMem = NULL;
    }
    if (m_dib.hBitmap) { DeleteObject(m_dib.hBitmap); m_dib.hBitmap = NULL; }
    m_dib.hOldBitmap = NULL; m_dib.pBits = NULL; m_dib.w = m_dib.h = 0;
}

bool OverlayRenderer::EnsureRenderTarget(UINT w, UINT h) {
    if (m_cache.rt && m_cache.rtW == w && m_cache.rtH == h) return true;
    m_cache.rt.Reset();
    m_cache.brush.Reset();
    m_cache.bmps.clear();
    m_cache.resourcesReady = false;

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
            D2D1_ALPHA_MODE_PREMULTIPLIED),
        0.0f, 0.0f, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);

    if (FAILED(g_d2dFactory->CreateDCRenderTarget(&rtProps, &m_cache.rt))) {
        m_cache.rt.Reset();
        m_cache.rtW = m_cache.rtH = 0;
        return false;
    }
    m_cache.rtW = w; m_cache.rtH = h;
    return true;
}

bool OverlayRenderer::EnsureDeviceResources(ID2D1RenderTarget* rt,
    const std::vector<OverlayImage>& images)
{
    if (m_cache.resourcesReady || !rt) return m_cache.resourcesReady;

    if (FAILED(rt->CreateSolidColorBrush(
        D2D1::ColorF(1, 1, 1, 1), &m_cache.brush)))
    {
        m_cache.brush.Reset();
        return false;
    }

    m_cache.bmps.assign(images.size(), nullptr);
    for (size_t i = 0; i < images.size(); ++i) {
        if (!images[i].pImage) continue;
        if (FAILED(rt->CreateBitmapFromWicBitmap(
            images[i].pImage.Get(), &m_cache.bmps[i])))
            m_cache.bmps[i].Reset();
    }
    m_cache.resourcesReady = true;
    return true;
}

void OverlayRenderer::DrawImageFrameAndHandles(ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush, const OverlayImage& img)
{
    float fx = DipToPx((float)img.x);
    float fy = DipToPx((float)img.y);
    float fw = DipToPx((float)img.width);
    float fh = DipToPx((float)img.height);
    float cx = fx + fw / 2.0f;
    float cy = fy + fh / 2.0f;

    rt->SetTransform(D2D1::Matrix3x2F::Rotation(img.rotation,
        D2D1::Point2F(cx, cy)));

    brush->SetColor(D2D1::ColorF(0.0f, 0.78f, 1.0f, 1.0f));
    D2D1_RECT_F border = D2D1::RectF(fx - 1.0f, fy - 1.0f,
        fx + fw + 1.0f, fy + fh + 1.0f);
    rt->DrawRectangle(&border, brush, 2.0f);

    float cs = DipToPx(14.0f);
    float right = fx + fw, bottom = fy + fh;

    D2D1_RECT_F h1 = D2D1::RectF(right - cs, bottom - cs, right, bottom);
    D2D1_RECT_F h2 = D2D1::RectF(right - cs, cy - cs / 2.0f, right, cy + cs / 2.0f);
    D2D1_RECT_F h3 = D2D1::RectF(cx - cs / 2.0f, bottom - cs, cx + cs / 2.0f, bottom);
    rt->FillRectangle(&h1, brush);
    rt->FillRectangle(&h2, brush);
    rt->FillRectangle(&h3, brush);

    float rotCx = fx + cs / 2.0f;
    float rotCy = fy + cs / 2.0f;
    float rr = cs / 2.0f;
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rotCx, rotCy), rr, rr), brush);

    rt->SetTransform(D2D1::Matrix3x2F::Identity());
}

bool OverlayRenderer::RenderToHDC(HDC hdc, int w, int h,
    const std::vector<OverlayImage>& images, bool editMode)
{
    if (!EnsureRenderTarget((UINT)w, (UINT)h)) return false;
    if (!m_cache.rt) return false;

    RECT rc = { 0, 0, w, h };
    if (FAILED(m_cache.rt->BindDC(hdc, &rc))) return false;

    if (!EnsureDeviceResources(m_cache.rt.Get(), images)) return false;
    if (!m_cache.brush) return false;

    m_cache.rt->BeginDraw();
    m_cache.rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    m_cache.rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_cache.rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    for (size_t i = 0; i < images.size(); ++i) {
        ID2D1Bitmap* bmp = (i < m_cache.bmps.size()) ? m_cache.bmps[i].Get() : nullptr;
        if (!bmp) continue;
        const OverlayImage& img = images[i];
        if (!img.visible) continue;

        float cx = DipToPx((float)img.x + img.width / 2.0f);
        float cy = DipToPx((float)img.y + img.height / 2.0f);
        float iw = DipToPx((float)img.width);
        float ih = DipToPx((float)img.height);

        m_cache.rt->SetTransform(D2D1::Matrix3x2F::Rotation(img.rotation,
            D2D1::Point2F(cx, cy)));
        D2D1_RECT_F dst = D2D1::RectF(cx - iw / 2.0f, cy - ih / 2.0f,
            cx + iw / 2.0f, cy + ih / 2.0f);
        m_cache.rt->DrawBitmap(bmp, &dst, 1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        m_cache.rt->SetTransform(D2D1::Matrix3x2F::Identity());
    }

    if (editMode) {
        for (const auto& img : images)
            if (img.pImage)
                DrawImageFrameAndHandles(m_cache.rt.Get(),
                    m_cache.brush.Get(), img);
    }

    HRESULT hr = m_cache.rt->EndDraw();
    if (FAILED(hr)) {
        InvalidateResources();
        return false;
    }
    return true;
}

bool OverlayRenderer::Render(HDC hdc, int screenW, int screenH,
    const std::vector<OverlayImage>& images, bool editMode)
{
    return RenderToHDC(hdc, screenW, screenH, images, editMode);
}