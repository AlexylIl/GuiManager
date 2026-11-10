// ============================================================================
//  overlay_window.cpp
// ============================================================================
#include "overlay.h"
#include "overlay_window.h"
#include "hotkey_editor.h"

// Время «грации» после выхода из диалога/режима редактирования,
// в течение которого оверлей не прячется по foreground-логике.
static const ULONGLONG EDIT_GRACE_MS = 3000;

// ============================================================================
//  ctor/dtor
// ============================================================================
OverlayWindow::OverlayWindow(HINSTANCE hInst, ConfigStore& cfg,
    ImageManager& im, TargetWatcher& watcher)
    : m_hInst(hInst), m_cfg(cfg), m_im(im), m_watcher(watcher) {}

OverlayWindow::~OverlayWindow() {
    if (m_hdcScreen) { ReleaseDC(NULL, m_hdcScreen); m_hdcScreen = NULL; }
    if (m_dib.hdcMem) {
        if (m_dib.hOldBitmap) SelectObject(m_dib.hdcMem, m_dib.hOldBitmap);
        DeleteDC(m_dib.hdcMem);
    }
    if (m_dib.hBitmap) DeleteObject(m_dib.hBitmap);
    m_cache.bmps.clear();
    m_cache.brush.Reset();
    m_cache.rt.Reset();
}

// ============================================================================
//  Create / RunMessageLoop
// ============================================================================
bool OverlayWindow::Create() {
    const wchar_t CLASS_NAME[] = L"OverlayWindowClass";
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = &OverlayWindow::StaticWndProc;
    wc.hInstance = m_hInst;
    wc.hIcon = LoadIconW(m_hInst, MAKEINTRESOURCEW(IDI_ICON1));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    wc.hIconSm = (HICON)LoadImageW(m_hInst, MAKEINTRESOURCEW(IDI_ICON1),
        IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    RegisterClassEx(&wc);

    int screenW = g_monRect.right - g_monRect.left;
    int screenH = g_monRect.bottom - g_monRect.top;

    m_hWnd = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        CLASS_NAME, L"Game Overlay", WS_POPUP,
        g_monRect.left, g_monRect.top, screenW, screenH,
        NULL, NULL, m_hInst, this); 

    if (!m_hWnd) return false;

    ApplyClickThrough(!m_editMode);
    UpdateDisplay();

    if (m_watcher.IsTargetForeground()) ShowWindow(m_hWnd, SW_SHOWNOACTIVATE);
    else                                ShowWindow(m_hWnd, SW_HIDE);

    return true;
}

void OverlayWindow::RunMessageLoop() {
    MSG msg = { 0 };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

// ============================================================================
//  Hotkeys
// ============================================================================
bool OverlayWindow::RegisterHotkeysNoMsgBox() {
    if (!m_hWnd) return false;

    struct Item { int id; UINT vk; };
    const Item items[] = {
        { HOTKEY_TOGGLE_VISIBLE, m_hotkeys.toggle_visible },
        { HOTKEY_TOGGLE_EDIT,    m_hotkeys.toggle_edit },
        { HOTKEY_RESET_POS,      m_hotkeys.reset_pos },
        { HOTKEY_RELOAD_IMAGES,  m_hotkeys.reload_images },
        { HOTKEY_CAPTURE_FG,     m_hotkeys.capture_fg },
        { HOTKEY_REMOVE_FG,      m_hotkeys.remove_fg },
        { HOTKEY_EDIT_HOTKEYS,   m_hotkeys.edit_hotkeys },
    };

    bool allOk = true;
    std::wstring warns;
    for (const auto& it : items) {
        if (!RegisterHotKey(m_hWnd, it.id, MOD_NOREPEAT, it.vk)) {
            allOk = false;
            wchar_t line[128];
            swprintf_s(line, L"  id=%d vk=0x%X\n", it.id, it.vk);
            warns += line;
        }
    }

    if (!allOk) {
        if (m_firstHotkeyRegister) {
            std::wstring msg =
                L"Some hotkeys could not be registered "
                L"(probably already used by another application):\n\n" +
                warns +
                L"\nEdit hotkeys with the '-' key.";
            RunWithDialog([&]() {
                MessageBoxW(m_hWnd, msg.c_str(), L"Hotkey registration",
                    MB_OK | MB_ICONWARNING | MB_TOPMOST);
                });
        }
        else {
            OutputDebugStringW(L"[OverlayWindow] hotkey registration failed:\n");
            OutputDebugStringW(warns.c_str());
        }
    }
    m_firstHotkeyRegister = false;
    return allOk;
}

void OverlayWindow::RegisterHotkeys() {
    RegisterHotkeysNoMsgBox();
}

void OverlayWindow::UnregisterAllHotkeys() {
    if (!m_hWnd) return;
    UnregisterHotKey(m_hWnd, HOTKEY_TOGGLE_VISIBLE);
    UnregisterHotKey(m_hWnd, HOTKEY_TOGGLE_EDIT);
    UnregisterHotKey(m_hWnd, HOTKEY_RESET_POS);
    UnregisterHotKey(m_hWnd, HOTKEY_RELOAD_IMAGES);
    UnregisterHotKey(m_hWnd, HOTKEY_CAPTURE_FG);
    UnregisterHotKey(m_hWnd, HOTKEY_REMOVE_FG);
    UnregisterHotKey(m_hWnd, HOTKEY_EDIT_HOTKEYS);
}

void OverlayWindow::ApplyHotkeysForProcess(const std::wstring& proc) {
    if (!m_hWnd) return;
    UnregisterAllHotkeys();
    m_hotkeys = m_cfg.LoadHotkeysForProcess(proc);
    RegisterHotkeys();
}

// ============================================================================
//  Initial
// ============================================================================
void OverlayWindow::InitialRender() {
    LoadImages();
    LoadLayoutForCurrentProcess();
    ApplyHotkeysForProcess(m_currentTargetProcess);
    UpdateDisplay();
}

// ============================================================================
//  Layout per-process
// ============================================================================
void OverlayWindow::MarkLayoutDirty() {
    m_layoutDirty = true;
    m_layoutDirtyOwner = m_currentTargetProcess;
}

void OverlayWindow::LoadLayoutForCurrentProcess() {
    if (m_layoutDirty && !m_layoutDirtyOwner.empty() &&
        m_layoutDirtyOwner != m_currentTargetProcess)
    {
        m_cfg.SaveLayoutForProcess(m_layoutDirtyOwner, g_images, false);
    }
    m_layoutDirty = false;
    m_layoutDirtyOwner.clear();
    m_cfg.LoadLayoutForProcess(m_currentTargetProcess, g_images);
}

bool OverlayWindow::SaveLayoutForCurrentProcess() {
    if (m_layoutDirtyOwner.empty() || !m_layoutDirty) return true;
    bool ok = m_cfg.SaveLayoutForProcess(m_layoutDirtyOwner, g_images, false);
    if (ok) { m_layoutDirty = false; m_layoutDirtyOwner.clear(); }
    return ok;
}

void OverlayWindow::SaveConfigIfDirty() {
    m_cfg.SaveAll(m_watcher.GetTargets(), {},
        m_layoutDirtyOwner, m_layoutDirty, g_images);
    m_layoutDirty = false;
    m_layoutDirtyOwner.clear();
}

// ============================================================================
//  Images
// ============================================================================
void OverlayWindow::LoadImages() {
    std::wstring diag;
    for (int i = 0; i < (int)g_images.size() && i < MAX_IMAGES; ++i) {
        bool usedCustom = false;
        HRESULT hr = m_im.LoadSlot(m_hInst, i, m_currentTargetProcess,
            g_images[i], usedCustom, &diag);
        if (SUCCEEDED(hr)) {
            SetImageWidthKeepAspect(g_images[i], DEFAULT_LAYOUT[i].w);
        }
    }
}

void OverlayWindow::ReloadImagesKeepingState() {
    m_cache.bmps.clear();
    m_cache.resourcesReady = false;

    std::wstring diag;
    m_im.ReloadAll(m_hInst, m_currentTargetProcess, g_images, &diag);
    UpdateDisplay();

    if (!diag.empty()) {
        std::wstring msg = L"Some images failed to load:\n\n" + diag;
        MessageBoxW(m_hWnd, msg.c_str(), L"Reload images",
            MB_OK | MB_ICONWARNING | MB_TOPMOST);
    }
}

// ============================================================================
//  D2D / DIB
// ============================================================================
bool OverlayWindow::EnsureDibCache(UINT w, UINT h) {
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
    HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
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

void OverlayWindow::ReleaseDib() {
    if (m_dib.hdcMem) {
        if (m_dib.hOldBitmap) SelectObject(m_dib.hdcMem, m_dib.hOldBitmap);
        DeleteDC(m_dib.hdcMem); m_dib.hdcMem = NULL;
    }
    if (m_dib.hBitmap) { DeleteObject(m_dib.hBitmap); m_dib.hBitmap = NULL; }
    m_dib.hOldBitmap = NULL; m_dib.pBits = NULL; m_dib.w = m_dib.h = 0;
}

void OverlayWindow::EnsureRenderTarget(UINT w, UINT h) {
    if (m_cache.rt && m_cache.rtW == w && m_cache.rtH == h) return;
    m_cache.rt.Reset(); m_cache.brush.Reset();
    m_cache.bmps.clear(); m_cache.resourcesReady = false;

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
            D2D1_ALPHA_MODE_PREMULTIPLIED),
        0.0f, 0.0f, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);

    if (FAILED(g_d2dFactory->CreateDCRenderTarget(&rtProps, &m_cache.rt))) {
        m_cache.rt.Reset(); m_cache.rtW = m_cache.rtH = 0; return;
    }
    m_cache.rtW = w; m_cache.rtH = h;
}

void OverlayWindow::EnsureDeviceResources() {
    if (m_cache.resourcesReady || !m_cache.rt) return;

    if (FAILED(m_cache.rt->CreateSolidColorBrush(
        D2D1::ColorF(1, 1, 1, 1), &m_cache.brush)))
    {
        m_cache.brush.Reset(); return;
    }

    m_cache.bmps.assign(g_images.size(), nullptr);
    for (size_t i = 0; i < g_images.size(); ++i) {
        if (!g_images[i].pImage) continue;
        if (FAILED(m_cache.rt->CreateBitmapFromWicBitmap(
            g_images[i].pImage.Get(), &m_cache.bmps[i])))
            m_cache.bmps[i].Reset();
    }
    m_cache.resourcesReady = true;
}

// ============================================================================
//  Render primitives
// ============================================================================
void OverlayWindow::DrawImageFrameAndHandles(ID2D1RenderTarget* rt,
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

void OverlayWindow::DrawVisButton(ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush, const OverlayImage& img)
{
    D2D1_RECT_F rd = GetVisButtonRectDip(img);
    D2D1_RECT_F box = D2D1::RectF(
        DipToPx(rd.left), DipToPx(rd.top),
        DipToPx(rd.right), DipToPx(rd.bottom));

    brush->SetColor(D2D1::ColorF(0, 0, 0, 0.35f));
    D2D1_RECT_F shadow = D2D1::RectF(box.left + 2, box.top + 2,
        box.right + 2, box.bottom + 2);
    rt->FillRectangle(&shadow, brush);

    if (img.visible) brush->SetColor(D2D1::ColorF(0.15f, 0.75f, 0.25f, 0.95f));
    else             brush->SetColor(D2D1::ColorF(0.80f, 0.20f, 0.20f, 0.95f));
    rt->FillRectangle(&box, brush);

    brush->SetColor(D2D1::ColorF(1, 1, 1, 1));
    rt->DrawRectangle(&box, brush, 2.0f);

    float ccx = (box.left + box.right) / 2.0f;
    float ccy = (box.top + box.bottom) / 2.0f;
    float halfPx = (box.right - box.left) / 2.0f;

    if (img.visible) {
        float k = halfPx * 0.55f;
        float gx = ccx - halfPx * 0.45f;
        float gy = ccy;
        rt->DrawLine(D2D1::Point2F(gx, gy),
            D2D1::Point2F(gx + k * 0.6f, gy + k * 0.6f), brush, 2.5f);
        rt->DrawLine(D2D1::Point2F(gx + k * 0.6f, gy + k * 0.6f),
            D2D1::Point2F(gx + k * 1.6f, gy - k * 0.8f), brush, 2.5f);
    }
    else {
        float k = halfPx * 0.45f;
        rt->DrawLine(D2D1::Point2F(ccx - k, ccy - k),
            D2D1::Point2F(ccx + k, ccy + k), brush, 2.5f);
        rt->DrawLine(D2D1::Point2F(ccx + k, ccy - k),
            D2D1::Point2F(ccx - k, ccy + k), brush, 2.5f);
    }
}

void OverlayWindow::DrawCloseButton(ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush, bool hover)
{
    int cx_i, cy_i;
    GetCloseButtonCenter(cx_i, cy_i);
    float ccx = DipToPx((float)cx_i);
    float ccy = DipToPx((float)cy_i);
    float r = DipToPx((float)CLOSE_BTN_SIZE) / 2.0f;

    brush->SetColor(D2D1::ColorF(0, 0, 0, 0.27f));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ccx + 2.0f, ccy + 2.0f), r, r), brush);

    if (hover) brush->SetColor(D2D1::ColorF(0.86f, 0.24f, 0.24f, 0.92f));
    else       brush->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.12f, 0.78f));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ccx, ccy), r, r), brush);

    brush->SetColor(D2D1::ColorF(1, 1, 1, 1));
    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(ccx, ccy), r, r), brush, 1.5f);

    float k = DipToPx(9.0f);
    rt->DrawLine(D2D1::Point2F(ccx - k, ccy - k),
        D2D1::Point2F(ccx + k, ccy + k), brush, 2.5f);
    rt->DrawLine(D2D1::Point2F(ccx + k, ccy - k),
        D2D1::Point2F(ccx - k, ccy + k), brush, 2.5f);
}

bool OverlayWindow::RenderToHDC(HDC hdc, int screenW, int screenH) {
    EnsureRenderTarget((UINT)screenW, (UINT)screenH);
    if (!m_cache.rt) return false;

    RECT rc = { 0, 0, screenW, screenH };
    if (FAILED(m_cache.rt->BindDC(hdc, &rc))) return false;

    EnsureDeviceResources();
    if (!m_cache.resourcesReady || !m_cache.brush) return false;

    m_cache.rt->BeginDraw();
    m_cache.rt->Clear(D2D1::ColorF(0, 0, 0, 0));   // п.27
    m_cache.rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_cache.rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    for (size_t i = 0; i < g_images.size(); ++i) {
        ID2D1Bitmap* bmp = (i < m_cache.bmps.size()) ? m_cache.bmps[i].Get() : nullptr;
        if (!bmp) continue;
        OverlayImage& img = g_images[i];
        if (!img.visible) continue;

        float cx = DipToPx((float)img.x + img.width / 2.0f);
        float cy = DipToPx((float)img.y + img.height / 2.0f);
        float w = DipToPx((float)img.width);
        float h = DipToPx((float)img.height);

        m_cache.rt->SetTransform(D2D1::Matrix3x2F::Rotation(img.rotation,
            D2D1::Point2F(cx, cy)));
        D2D1_RECT_F dst = D2D1::RectF(cx - w / 2.0f, cy - h / 2.0f,
            cx + w / 2.0f, cy + h / 2.0f);
        m_cache.rt->DrawBitmap(bmp, &dst, 1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        m_cache.rt->SetTransform(D2D1::Matrix3x2F::Identity());
    }

    if (m_editMode) {
        for (auto& img : g_images)
            if (img.pImage)
                DrawImageFrameAndHandles(m_cache.rt.Get(), m_cache.brush.Get(), img);
        for (auto& img : g_images)
            if (img.pImage)
                DrawVisButton(m_cache.rt.Get(), m_cache.brush.Get(), img);
        DrawCloseButton(m_cache.rt.Get(), m_cache.brush.Get(), m_closeBtnHover);
    }

    HRESULT hr = m_cache.rt->EndDraw();
    if (FAILED(hr)) {
        m_cache.rt.Reset(); m_cache.brush.Reset();
        m_cache.bmps.clear(); m_cache.resourcesReady = false;
        m_cache.rtW = m_cache.rtH = 0;
        return false;
    }
    return true;
}

void OverlayWindow::UpdateDisplay() {
    if (!m_hWnd) return;
    int screenW = g_monRect.right - g_monRect.left;
    int screenH = g_monRect.bottom - g_monRect.top;
    if (screenW <= 0 || screenH <= 0) return;

    if (!EnsureDibCache((UINT)screenW, (UINT)screenH)) {
        OutputDebugStringW(L"[OverlayWindow] UpdateDisplay: EnsureDibCache failed\n");
        return;
    }
    if (!RenderToHDC(m_dib.hdcMem, screenW, screenH)) {
        OutputDebugStringW(L"[OverlayWindow] UpdateDisplay: RenderToHDC failed\n");
        memset(m_dib.pBits, 0, (size_t)screenW * (size_t)screenH * 4);
    }

    if (!m_hdcScreen) {
        m_hdcScreen = GetDC(NULL);
        if (!m_hdcScreen) return;
    }

    POINT ptSrc = { 0, 0 };
    SIZE  sizeWnd = { screenW, screenH };
    POINT ptDst = { g_monRect.left, g_monRect.top };
    BLENDFUNCTION blend = { 0 };
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    if (!UpdateLayeredWindow(m_hWnd, m_hdcScreen, &ptDst, &sizeWnd,
        m_dib.hdcMem, &ptSrc, 0, &blend, ULW_ALPHA))
    {
        OutputDebugStringW(L"[OverlayWindow] UpdateDisplay: UpdateLayeredWindow failed\n");
    }
}

void OverlayWindow::InvalidateDibAndRedraw() {
    ReleaseDib();
    UpdateDisplay();
}

// ============================================================================
//  Click-through
// ============================================================================
void OverlayWindow::ApplyClickThrough(bool clickThrough) {
    if (!m_hWnd) return;
    LONG_PTR ex = GetWindowLongPtr(m_hWnd, GWL_EXSTYLE);
    if (clickThrough) ex |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    else              ex &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    SetWindowLongPtr(m_hWnd, GWL_EXSTYLE, ex);
    SetWindowPos(m_hWnd, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

// ============================================================================
//  RunWithDialog
// ============================================================================
template <typename Fn>
void OverlayWindow::RunWithDialog(Fn&& showDialog) {
    if (m_dialogOpen) return;
    m_dialogOpen = true;

    HWND hPrevFg = GetForegroundWindow();
    bool wasTarget = false;
    if (hPrevFg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hPrevFg, &pid);
        wchar_t name[MAX_PATH] = { 0 };
        if (TargetWatcher::GetProcessNameById(pid, name, MAX_PATH) &&
            m_watcher.IsProcessInTargetList(name))
            wasTarget = true;
    }

    KillTimer(m_hWnd, TIMER_WATCH_GAME);


    showDialog();

    m_dialogOpen = false;
    if (!IsWindow(m_hWnd)) return;

    SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    if (wasTarget && hPrevFg && IsWindow(hPrevFg))
        SetForegroundWindow(hPrevFg);

    SetTimer(m_hWnd, TIMER_WATCH_GAME, GAME_POLL_INTERVAL_MS, NULL);
    m_graceUntilTick = GetTickCount64() + 1000;
}

// ============================================================================
//  Hotkey handlers
// ============================================================================
void OverlayWindow::HandleToggleVisible() {
    if (IsWindowVisible(m_hWnd)) {
        m_userHidden = true;
        ShowWindow(m_hWnd, SW_HIDE);
    }
    else {
        m_userHidden = false;
        m_watcher.RefreshMonitorRect(m_hWnd, this);
        ShowWindow(m_hWnd, SW_SHOWNOACTIVATE);
        UpdateDisplay();
    }
}

void OverlayWindow::HandleToggleEdit() {
    if (!m_editMode) {
        if (!m_watcher.IsTargetForeground()) return;
    }
    m_editMode = !m_editMode;

    if (m_editMode) {
        m_userHidden = false;
        m_watcher.RefreshMonitorRect(m_hWnd, this);
        ShowWindow(m_hWnd, SW_SHOWNOACTIVATE);
    }
    else {
        m_graceUntilTick = GetTickCount64() + EDIT_GRACE_MS;   

        HWND hTarget = NULL;
        HWND hFg = GetForegroundWindow();
        if (hFg) {
            DWORD pid = 0;
            GetWindowThreadProcessId(hFg, &pid);
            wchar_t name[MAX_PATH] = { 0 };
            if (TargetWatcher::GetProcessNameById(pid, name, MAX_PATH) &&
                m_watcher.IsProcessInTargetList(name))
                hTarget = hFg;
        }
        if (!hTarget) {
            struct Ctx { TargetWatcher* w; HWND h; } ctx = { &m_watcher, NULL };
            EnumWindows([](HWND h, LPARAM lp) -> BOOL {
                auto* c = reinterpret_cast<Ctx*>(lp);
                if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER) != NULL) return TRUE;
                DWORD pid = 0;
                GetWindowThreadProcessId(h, &pid);
                wchar_t name[MAX_PATH] = { 0 };
                if (!TargetWatcher::GetProcessNameById(pid, name, MAX_PATH)) return TRUE;
                if (TargetWatcher::IsIgnoredProcess(name)) return TRUE;
                if (c->w->IsProcessInTargetList(name)) { c->h = h; return FALSE; }
                return TRUE;
                }, reinterpret_cast<LPARAM>(&ctx));
            hTarget = ctx.h;
        }
        if (hTarget) SetForegroundWindow(hTarget);
    }

    ApplyClickThrough(!m_editMode);
    for (auto& img : g_images) {
        img.isDragging = img.isResizing = img.isRotating = false;
        img.resizeMode = RESIZE_NONE;
    }
    m_activeImgIdx = -1;
    if (GetCapture() == m_hWnd) ReleaseCapture();
    m_closeBtnHover = false;
    m_lastRotateClickImgIdx = -1;
    m_lastRotateClickTick = 0;
    UpdateDisplay();

    if (!m_editMode && m_layoutDirty) SaveConfigIfDirty();
}

void OverlayWindow::HandleReloadImages() {
    m_currentTargetProcess = m_watcher.ResolveCurrentTargetProcess();
    LoadLayoutForCurrentProcess();
    RunWithDialog([&]() { ReloadImagesKeepingState(); });
    MarkLayoutDirty();
}

void OverlayWindow::HandleResetPos() {
    if (!m_watcher.IsTargetForeground()) return;
    RunWithDialog([&]() {
        int res = MessageBoxW(m_hWnd,
            L"Reset all images to default position?\n\n"
            L"This will discard current layout and rotation.",
            L"Reset layout", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST);
        if (res == IDYES) {
            ConfigStore::ResetToDefaults(g_images);
            MarkLayoutDirty();
            UpdateDisplay();
            SaveConfigIfDirty();
        }
        });
}

void OverlayWindow::HandleCaptureForeground() {
    HWND hFg = GetForegroundWindow();
    if (!hFg || hFg == m_hWnd) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(hFg, &pid);
    wchar_t name[MAX_PATH] = { 0 };
    if (!TargetWatcher::GetProcessNameById(pid, name, MAX_PATH)) return;

    if (TargetWatcher::IsIgnoredProcess(name)) {
        RunWithDialog([&]() {
            wchar_t msg[512];
            swprintf_s(msg,
                L"This process is in the ignore list and cannot be a target:\n%s\n\n"
                L"(Edit IsIgnoredProcess() in the source to change the list.)",
                name);
            MessageBoxW(m_hWnd, msg, L"Capture foreground",
                MB_OK | MB_ICONWARNING | MB_TOPMOST);
            });
        return;
    }

    TargetPolicy& targets = m_watcher.MutableTargets();
    bool exists = false;
    for (auto& p : targets.processes)
        if (_wcsicmp(p.c_str(), name) == 0) { exists = true; break; }

    RunWithDialog([&]() {
        if (!exists) {
            targets.processes.push_back(name);
            targets.showEverywhere = false;
            SaveConfigIfDirty();

            m_cfg.SaveLayoutForProcess(name, g_images, /*onlyIfEmpty*/ true);
            m_cfg.SaveHotkeysForProcess(name, ConfigStore::DefaultHotkeys(),
                /*onlyIfEmpty*/ true);

            m_currentTargetProcess = name;
            ApplyHotkeysForProcess(m_currentTargetProcess);
            LoadLayoutForCurrentProcess();

            m_cache.bmps.clear();
            m_cache.resourcesReady = false;
            std::wstring diag;
            for (int i = 0; i < (int)g_images.size() && i < MAX_IMAGES; ++i) {
                bool usedCustom = false;
                m_im.LoadSlot(m_hInst, i, m_currentTargetProcess,
                    g_images[i], usedCustom, &diag);
            }

            m_watcher.RefreshMonitorRect(m_hWnd, this);
            UpdateDisplay();

            wchar_t msg[512];
            swprintf_s(msg, L"Added target process:\n%s\n\nTotal targets: %d",
                name, (int)targets.processes.size());
            MessageBoxW(m_hWnd, msg, L"Capture foreground",
                MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        }
        else {
            wchar_t msg[512];
            swprintf_s(msg, L"This process is already in the list:\n%s", name);
            MessageBoxW(m_hWnd, msg, L"Capture foreground",
                MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        }
        });
}

void OverlayWindow::HandleRemoveForeground() {
    HWND hFg = GetForegroundWindow();
    if (!hFg || hFg == m_hWnd) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(hFg, &pid);
    wchar_t name[MAX_PATH] = { 0 };
    if (!TargetWatcher::GetProcessNameById(pid, name, MAX_PATH)) return;

    TargetPolicy& targets = m_watcher.MutableTargets();
    auto it = std::find_if(targets.processes.begin(), targets.processes.end(),
        [&](const std::wstring& p) { return wcscmp(p.c_str(), name) == 0; });

    RunWithDialog([&]() {
        if (it == targets.processes.end() && !targets.showEverywhere) {
            wchar_t msg[512];
            swprintf_s(msg, L"Process not in target list:\n%s", name);
            MessageBoxW(m_hWnd, msg, L"Remove foreground",
                MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
            return;
        }
        if (targets.showEverywhere) {
            MessageBoxW(m_hWnd,
                L"Overlay is currently shown for ANY window ('*' in config).\n"
                L"Nothing to remove from the target list.\n\n"
                L"Edit target_processes in the INI to disable this mode.",
                L"Remove foreground", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
            return;
        }
        targets.processes.erase(it);
        if (targets.processes.empty()) targets.showEverywhere = false;
        SaveConfigIfDirty();

        m_currentTargetProcess = m_watcher.ResolveCurrentTargetProcess();
        ApplyHotkeysForProcess(m_currentTargetProcess);
        LoadLayoutForCurrentProcess();
        m_watcher.RefreshMonitorRect(m_hWnd, this);
        UpdateDisplay();

        wchar_t msg[512];
        if (targets.processes.empty()) {
            swprintf_s(msg,
                L"Removed target process:\n%s\n\n"
                L"No targets left - overlay will stay hidden.", name);
        }
        else {
            swprintf_s(msg,
                L"Removed target process:\n%s\n\nRemaining targets: %d",
                name, (int)targets.processes.size());
        }
        MessageBoxW(m_hWnd, msg, L"Remove foreground",
            MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        });
}

void OverlayWindow::HandleEditHotkeys() {
    if (m_currentTargetProcess.empty()) {
        MessageBoxW(m_hWnd,
            L"Activate the game window - hotkeys are configured per target process.\n"
            L"(In '*' mode, the global set cannot be edited.)",
            L"Configuring hotkeys", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        return;
    }

    RunWithDialog([&]() {
        UnregisterAllHotkeys();

        HotkeyConfig edited = m_hotkeys;
        bool exitRequested = false;
        bool saved = HotkeyEditorWindow::ShowModal(m_hWnd, m_hInst,
            edited, &exitRequested);

        if (exitRequested) {
            DestroyWindow(m_hWnd);
            return;
        }

        if (!saved) {
            RegisterHotkeys();
            return;
        }

        HotkeyConfig backup = m_hotkeys;
        m_hotkeys = edited;

        if (!RegisterHotkeysNoMsgBox()) {
            UnregisterAllHotkeys();
            m_hotkeys = backup;
            RegisterHotkeys();
            MessageBoxW(m_hWnd,
                L"Failed to register a new set of hotkeys.\n"
                L"Most likely, some keys are being used by other programs.\n\n"
                L"Changes have not been saved.",
                L"Configuring hotkeys", MB_OK | MB_ICONWARNING | MB_TOPMOST);
            return;
        }

        m_cfg.SaveHotkeysForProcess(m_currentTargetProcess, m_hotkeys,
            /*onlyIfEmpty*/ false);

        wchar_t msg[256];
        swprintf_s(msg, L"Hotkeys saved for the process:\n%s",
            m_currentTargetProcess.c_str());
        MessageBoxW(m_hWnd, msg, L"Configuring hotkeys",
            MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        });
}

void OverlayWindow::HandleTimer(WPARAM wParam) {
    if (wParam == TIMER_KEEP_TOPMOST) {
        if (m_dialogOpen) return;
        SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return;
    }
    if (wParam == TIMER_WATCH_GAME) {
        if (!m_editMode) {
            std::wstring newProc = m_watcher.ResolveCurrentTargetProcess();
            if (!newProc.empty() && newProc != m_currentTargetProcess) {
                bool canSwitch = true;
                if (!m_currentTargetProcess.empty() && m_layoutDirty) {
                    if (!SaveLayoutForCurrentProcess()) canSwitch = false;
                }
                if (canSwitch) {
                    m_currentTargetProcess = newProc;
                    ApplyHotkeysForProcess(m_currentTargetProcess);
                    LoadLayoutForCurrentProcess();
                    m_cache.bmps.clear();
                    m_cache.resourcesReady = false;
                    std::wstring diag;
                    for (int i = 0; i < (int)g_images.size() && i < MAX_IMAGES; ++i) {
                        bool usedCustom = false;
                        m_im.LoadSlot(m_hInst, i, m_currentTargetProcess,
                            g_images[i], usedCustom, &diag);
                    }
                    UpdateDisplay();
                }
            }
        }

        int state = m_watcher.CheckAnyTargetRunning();
        if (state < 0) return;

        m_watcher.RefreshMonitorRect(m_hWnd, this);
        if (m_editMode) return;
        if (m_dialogOpen) return;

        if (state == 0) {
            if (IsWindowVisible(m_hWnd)) ShowWindow(m_hWnd, SW_HIDE);
            return;
        }

        bool fg = m_watcher.IsTargetForegroundCached();
        ULONGLONG now = GetTickCount64();
        bool inGrace = (m_graceUntilTick != 0) && (m_graceUntilTick > now);
        if (!inGrace) m_graceUntilTick = 0;

        if (m_userHidden) {
            if (IsWindowVisible(m_hWnd)) ShowWindow(m_hWnd, SW_HIDE);
            return;
        }
        if (fg || inGrace) {
            if (!IsWindowVisible(m_hWnd)) ShowWindow(m_hWnd, SW_SHOWNOACTIVATE);
        }
        else {
            if (IsWindowVisible(m_hWnd)) ShowWindow(m_hWnd, SW_HIDE);
        }
    }
}

// ============================================================================
//  Mouse
// ============================================================================
void OverlayWindow::HandleMouseWheel(WPARAM wParam, LPARAM lParam) {
    if (!m_editMode) return;
    int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    ScreenToClient(m_hWnd, &pt);
    for (int i = (int)g_images.size() - 1; i >= 0; --i) {
        if (IsPointInImage(g_images[i], pt.x, pt.y)) {
            ZoomImage(g_images[i], delta);
            MarkLayoutDirty();
            UpdateDisplay();
            return;
        }
    }
}

void OverlayWindow::HandleLButtonDown(LPARAM lParam) {
    if (!m_editMode) return;
    int pxX = GET_X_LPARAM(lParam);
    int pxY = GET_Y_LPARAM(lParam);
    int x = PxToDipI(pxX), y = PxToDipI(pxY);

    if (IsPointInCloseButton(pxX, pxY, m_editMode)) { DestroyWindow(m_hWnd); return; }

    for (auto& img : g_images) {
        img.isDragging = img.isResizing = img.isRotating = false;
        img.resizeMode = RESIZE_NONE;
    }
    m_activeImgIdx = -1;
    if (GetCapture() == m_hWnd) ReleaseCapture();

    for (int i = (int)g_images.size() - 1; i >= 0; --i) {
        OverlayImage& im = g_images[i];
        if (!im.pImage) continue;
        if (IsPointInVisButton(im, pxX, pxY)) {
            im.visible = !im.visible;
            MarkLayoutDirty();
            UpdateDisplay();
            return;
        }
    }

    for (int i = (int)g_images.size() - 1; i >= 0; --i) {
        OverlayImage& im = g_images[i];
        if (!im.pImage) continue;
        HitMask hm = ComputeHitMask(im, pxX, pxY);

        if (hm.inRotate) {
            ULONGLONG now = GetTickCount64();
            ULONGLONG dblMs = (ULONGLONG)GetDoubleClickTime();
            bool isDouble = (m_lastRotateClickImgIdx == i) &&
                (now - m_lastRotateClickTick <= dblMs);
            if (isDouble) {
                im.rotation = 0.0f;
                MarkLayoutDirty();
                m_lastRotateClickImgIdx = -1;
                m_lastRotateClickTick = 0;
                UpdateDisplay();
                return;
            }
            m_lastRotateClickImgIdx = i;
            m_lastRotateClickTick = now;
            m_activeImgIdx = i;
            im.isRotating = true;
            StartRotate(im, pxX, pxY);
            SetCapture(m_hWnd);
            return;
        }
        if (hm.inResizeCorner) {
            m_activeImgIdx = i; im.isResizing = true;
            im.resizeMode = RESIZE_CORNER;
            SetCapture(m_hWnd); return;
        }
        if (hm.inRightEdge) {
            m_activeImgIdx = i; im.isResizing = true;
            im.resizeMode = RESIZE_WIDTH_ONLY;
            SetCapture(m_hWnd); return;
        }
        if (hm.inBottomEdge) {
            m_activeImgIdx = i; im.isResizing = true;
            im.resizeMode = RESIZE_HEIGHT_ONLY;
            SetCapture(m_hWnd); return;
        }
        if (hm.inImage) {
            m_activeImgIdx = i; im.isDragging = true;
            im.dragOffsetX = x - im.x; im.dragOffsetY = y - im.y;
            SetCapture(m_hWnd); return;
        }
    }
}

void OverlayWindow::HandleMouseMove(LPARAM lParam) {
    if (!m_editMode) return;
    if (!m_trackingMouse) {
        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT) };
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = m_hWnd;
        TrackMouseEvent(&tme);
        m_trackingMouse = true;
    }

    int pxX = GET_X_LPARAM(lParam);
    int pxY = GET_Y_LPARAM(lParam);
    int x = PxToDipI(pxX), y = PxToDipI(pxY);

    bool hover = IsPointInCloseButton(pxX, pxY, m_editMode);
    if (hover != m_closeBtnHover) {
        m_closeBtnHover = hover;
        UpdateDisplay();
    }

    if (m_activeImgIdx >= 0 && m_activeImgIdx < (int)g_images.size()) {
        OverlayImage& im = g_images[m_activeImgIdx];
        if (im.isDragging) {
            im.x = x - im.dragOffsetX;
            im.y = y - im.dragOffsetY;
            MarkLayoutDirty(); UpdateDisplay();
        }
        else if (im.isRotating) {
            UpdateRotate(im, pxX, pxY);
            MarkLayoutDirty(); UpdateDisplay();
        }
        else if (im.isResizing) {
            switch (im.resizeMode) {
            case RESIZE_CORNER:      ResizeCorner(im, pxX, pxY);     break;
            case RESIZE_WIDTH_ONLY:  ResizeWidthOnly(im, pxX, pxY);  break;
            case RESIZE_HEIGHT_ONLY: ResizeHeightOnly(im, pxX, pxY); break;
            default: break;
            }
            MarkLayoutDirty(); UpdateDisplay();
        }
    }
}

void OverlayWindow::HandleLButtonUp() {
    if (m_activeImgIdx >= 0 && m_activeImgIdx < (int)g_images.size()) {
        OverlayImage& im = g_images[m_activeImgIdx];
        im.isDragging = im.isResizing = im.isRotating = false;
        im.resizeMode = RESIZE_NONE;
        m_activeImgIdx = -1;
        if (GetCapture() == m_hWnd) ReleaseCapture();
    }
}

void OverlayWindow::UpdateCursorForPoint(int pxX, int pxY) {
    if (IsPointInCloseButton(pxX, pxY, m_editMode)) {
        SetCursor(LoadCursor(NULL, IDC_HAND)); return;
    }
    for (int i = (int)g_images.size() - 1; i >= 0; --i) {
        OverlayImage& im = g_images[i];
        if (!im.pImage) continue;
        if (IsPointInVisButton(im, pxX, pxY)) {
            SetCursor(LoadCursor(NULL, IDC_HAND)); return;
        }
    }
    for (int i = (int)g_images.size() - 1; i >= 0; --i) {
        OverlayImage& im = g_images[i];
        if (!im.pImage) continue;
        HitMask hm = ComputeHitMask(im, pxX, pxY);
        if (hm.inRotate) { SetCursor(LoadCursor(NULL, IDC_CROSS));   return; }
        if (hm.inResizeCorner) { SetCursor(LoadCursor(NULL, IDC_SIZENWSE)); return; }
        if (hm.inRightEdge) { SetCursor(LoadCursor(NULL, IDC_SIZEWE));  return; }
        if (hm.inBottomEdge) { SetCursor(LoadCursor(NULL, IDC_SIZENS));  return; }
        if (hm.inImage) { SetCursor(LoadCursor(NULL, IDC_SIZEALL)); return; }
    }
    SetCursor(LoadCursor(NULL, IDC_ARROW));
}

// ============================================================================
//  WndProc
// ============================================================================
LRESULT CALLBACK OverlayWindow::StaticWndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l) {
    OverlayWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        self = reinterpret_cast<OverlayWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hWnd = hWnd;
    }
    else {
        self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }
    if (self) return self->WndProc(hWnd, msg, w, l);
    return DefWindowProcW(hWnd, msg, w, l);
}

LRESULT OverlayWindow::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        // п.1 не применяем — оставляем как есть.
    case WM_NCHITTEST:
        if (!m_editMode) return HTTRANSPARENT;
        return DefWindowProcW(hWnd, message, wParam, lParam);

    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT && m_editMode) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hWnd, &pt);
            UpdateCursorForPoint(pt.x, pt.y);
            return TRUE;
        }
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }

    case WM_CAPTURECHANGED:
        if (m_activeImgIdx >= 0 && m_activeImgIdx < (int)g_images.size()) {
            OverlayImage& im = g_images[m_activeImgIdx];
            im.isDragging = im.isResizing = im.isRotating = false;
            im.resizeMode = RESIZE_NONE;
        }
        m_activeImgIdx = -1;
        m_trackingMouse = false;
        if (m_closeBtnHover) {
            m_closeBtnHover = false;
            UpdateDisplay();
        }
        break;

    case WM_DISPLAYCHANGE:
        m_watcher.InvalidateCaches();
        m_watcher.RefreshMonitorRect(hWnd, this);
        break;

    case WM_DPICHANGED: {
        UINT newDpiX = LOWORD(wParam);
        UINT newDpiY = HIWORD(wParam);
        UINT newDpi = newDpiX ? newDpiX : newDpiY;
        if (newDpi != 0) g_currentDpi = newDpi;
        ReleaseDib();
        m_watcher.RefreshMonitorRect(hWnd, this);
        UpdateDisplay();
        break;
    }

    case WM_APP_SHOW_OVERLAY:
        m_userHidden = false;
        m_watcher.RefreshMonitorRect(hWnd, this);
        ShowWindow(hWnd, SW_SHOWNOACTIVATE);
        UpdateDisplay();
        break;

    case WM_HOTKEY:
        if (wParam == HOTKEY_TOGGLE_VISIBLE)  HandleToggleVisible();
        else if (wParam == HOTKEY_TOGGLE_EDIT)     HandleToggleEdit();
        else if (wParam == HOTKEY_RESET_POS)       HandleResetPos();
        else if (wParam == HOTKEY_RELOAD_IMAGES)   HandleReloadImages();
        else if (wParam == HOTKEY_CAPTURE_FG)      HandleCaptureForeground();
        else if (wParam == HOTKEY_REMOVE_FG)       HandleRemoveForeground();
        else if (wParam == HOTKEY_EDIT_HOTKEYS)    HandleEditHotkeys();
        break;

    case WM_TIMER:       HandleTimer(wParam); break;
    case WM_MOUSEWHEEL:  HandleMouseWheel(wParam, lParam); break;
    case WM_LBUTTONDOWN: HandleLButtonDown(lParam); break;
    case WM_MOUSEMOVE:   HandleMouseMove(lParam); break;

    case WM_MOUSELEAVE:
        m_trackingMouse = false;
        if (m_closeBtnHover) {
            m_closeBtnHover = false;
            UpdateDisplay();
        }
        break;

    case WM_LBUTTONUP:   HandleLButtonUp(); break;

    case WM_DESTROY:
        KillTimer(hWnd, TIMER_KEEP_TOPMOST);
        KillTimer(hWnd, TIMER_WATCH_GAME);
        SaveConfigIfDirty();
        UnregisterAllHotkeys();
        ReleaseDib();
        if (m_hdcScreen) { ReleaseDC(NULL, m_hdcScreen); m_hdcScreen = NULL; }
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}