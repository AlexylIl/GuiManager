// ============================================================================
//  overlay_window.h
// ============================================================================
#pragma once

#include "overlay.h"
#include "config_store.h"
#include "image_manager.h"
#include "target_watcher.h"
#include "overlay_renderer.h"
#include "layout_store.h"
#include "hotkey_manager.h"

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

    void SetCurrentTargetProcess(const std::wstring& t);

    void InitialRender();

    void ReleaseDib();
    void UpdateDisplay();

private:
    static LRESULT CALLBACK StaticWndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

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

    void LoadImages();
    void ReloadImagesKeepingState();

    template <typename Fn>
    void RunWithDialog(Fn&& fn);

    HINSTANCE       m_hInst;
    ConfigStore& m_cfg;
    ImageManager& m_im;
    TargetWatcher& m_watcher;

    OverlayRenderer m_renderer;
    LayoutStore     m_layout;
    HotkeyManager   m_hotkeysMgr;

    HWND            m_hWnd = NULL;

    bool  m_userHidden = false;
    bool  m_editMode = false;
    bool  m_dialogOpen = false;
    bool  m_trackingMouse = false;
    int   m_activeImgIdx = -1;
    int   m_lastRotateClickImgIdx = -1;
    ULONGLONG m_lastRotateClickTick = 0;
    ULONGLONG m_graceUntilTick = 0;

    std::wstring m_currentTargetProcess;
    std::wstring m_lastConfiguredProcess;

    HDC m_hdcScreen = NULL;
};