// ============================================================================
//  overlay_window.h
// ============================================================================
#pragma once

#include "overlay.h"
#include "config_store.h"
#include "image_manager.h"
#include "target_watcher.h"

class OverlayWindow {
public:
    OverlayWindow(HINSTANCE hInst, ConfigStore& cfg, ImageManager& im,
        TargetWatcher& watcher);
    ~OverlayWindow();

    bool Create();
    void InvalidateDibAndRedraw();
    void ApplyClickThrough(bool clickThrough);
    void RunMessageLoop();

    HWND Handle() const { return m_hWnd; }
    bool IsEditMode() const { return m_editMode; }

    void SetCurrentTargetProcess(const std::wstring& t) { m_currentTargetProcess = t; }

    void InitialRender();

    void ReleaseDib();
    void UpdateDisplay();

private:
    static LRESULT CALLBACK StaticWndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    void RegisterHotkeys();
    void UnregisterAllHotkeys();
    void ApplyHotkeysForProcess(const std::wstring& proc);

    void HandleToggleVisible();
    void HandleToggleEdit();
    void HandleReloadImages();
    void HandleResetPos();
    void HandleCaptureForeground();
    void HandleRemoveForeground();
    void HandleEditHotkeys();
    void HandleTimer(WPARAM wParam);

    void HandleMouseWheel(WPARAM, LPARAM);
    void HandleLButtonDown(LPARAM);
    void HandleMouseMove(LPARAM);
    void HandleLButtonUp();
    void UpdateCursorForPoint(int pxX, int pxY);

    void LoadLayoutForCurrentProcess();
    bool SaveLayoutForCurrentProcess();
    void MarkLayoutDirty();
    void SaveConfigIfDirty();

    void LoadImages();
    void ReloadImagesKeepingState();

    bool RegisterHotkeysNoMsgBox();

    bool EnsureDibCache(UINT w, UINT h);
    void EnsureRenderTarget(UINT w, UINT h);
    void EnsureDeviceResources();
    bool RenderToHDC(HDC hdc, int w, int h);
    void DrawImageFrameAndHandles(ID2D1RenderTarget* rt,
        ID2D1SolidColorBrush* brush,
        const OverlayImage& img);
    void DrawVisButton(ID2D1RenderTarget* rt,
        ID2D1SolidColorBrush* brush,
        const OverlayImage& img);
    void DrawCloseButton(ID2D1RenderTarget* rt,
        ID2D1SolidColorBrush* brush, bool hover);

    template <typename Fn>
    void RunWithDialog(Fn&& fn);

    HINSTANCE       m_hInst;
    ConfigStore& m_cfg;
    ImageManager& m_im;
    TargetWatcher& m_watcher;
    HWND            m_hWnd = NULL;

    bool  m_userHidden = false;
    bool  m_editMode = false;
    bool  m_dialogOpen = false;
    bool  m_closeBtnHover = false;
    bool  m_trackingMouse = false;
    int   m_activeImgIdx = -1;
    int   m_lastRotateClickImgIdx = -1;
    ULONGLONG m_lastRotateClickTick = 0;
    ULONGLONG m_graceUntilTick = 0;

    std::wstring m_currentTargetProcess;
    std::wstring m_layoutDirtyOwner;
    bool         m_layoutDirty = false;

    HotkeyConfig m_hotkeys;
    bool         m_firstHotkeyRegister = true;

    HDC m_hdcScreen = NULL;

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