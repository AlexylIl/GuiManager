// ============================================================================
//  overlay_window.cpp
// ============================================================================
#include "overlay.h"
#include "ui_kit.h"
#include "overlay_window.h"
#include "hotkey_editor.h"
#include "process_picker.h"

static const ULONGLONG EDIT_GRACE_MS = 3000;

OverlayWindow::OverlayWindow(HINSTANCE hInst, ConfigStore& cfg,
    ImageManager& im, TargetWatcher& watcher)
    : m_hInst(hInst), m_cfg(cfg), m_im(im), m_watcher(watcher),
    m_layout(cfg, L""), m_hotkeysMgr(cfg, hInst) {}

OverlayWindow::~OverlayWindow() {
    if (m_hdcScreen) { ReleaseDC(NULL, m_hdcScreen); m_hdcScreen = NULL; }
}

void OverlayWindow::SetCurrentTargetProcess(const std::wstring& t) {
    m_currentTargetProcess = t;
    m_layout.SetOwner(t);
}

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

    m_hotkeysMgr.SetWindow(m_hWnd);

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

void OverlayWindow::InitialRender() {
    LoadImages();
    m_layout.LoadForCurrent(g_images);
    m_hotkeysMgr.ApplyForProcess(m_currentTargetProcess);
    UpdateDisplay();
}

void OverlayWindow::LoadImages() {
    std::wstring diag;
    for (int i = 0; i < (int)g_images.size() && i < MAX_IMAGES; ++i) {
        bool usedCustom = false;
        HRESULT hr = m_im.LoadSlot(m_hInst, i, m_currentTargetProcess,
            g_images[i], usedCustom, &diag);
        if (SUCCEEDED(hr))
            SetImageWidthKeepAspect(g_images[i], DEFAULT_LAYOUT[i].w);
    }
}

void OverlayWindow::ReloadImagesKeepingState() {
    m_renderer.InvalidateResources();

    std::wstring diag;
    m_im.ReloadAll(m_hInst, m_currentTargetProcess, g_images, &diag);
    UpdateDisplay();

    if (!diag.empty()) {
        std::wstring msg = L"Some images failed to load:\n\n" + diag;
        UiKit::ShowWarning(m_hWnd, m_hInst, L"Reload images", msg.c_str());
    }
}

void OverlayWindow::UpdateDisplay() {
    if (!m_hWnd) return;
    int screenW = g_monRect.right - g_monRect.left;
    int screenH = g_monRect.bottom - g_monRect.top;
    if (screenW <= 0 || screenH <= 0) return;

    if (!m_renderer.EnsureDib((UINT)screenW, (UINT)screenH)) {
        OutputDebugStringW(L"[OverlayWindow] EnsureDib failed\n");
        return;
    }

    if (!m_renderer.Render(m_renderer.DibHdc(), screenW, screenH,
        g_images, m_editMode))
    {
        OutputDebugStringW(L"[OverlayWindow] Render failed\n");
        memset(m_renderer.DibBits(), 0,
            (size_t)screenW * (size_t)screenH * 4);
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
        m_renderer.DibHdc(), &ptSrc, 0, &blend, ULW_ALPHA))
    {
        OutputDebugStringW(L"[OverlayWindow] UpdateLayeredWindow failed\n");
    }
}

void OverlayWindow::InvalidateDibAndRedraw() {
    ReleaseDib();
    UpdateDisplay();
}

void OverlayWindow::ReleaseDib() {
    m_renderer.ReleaseDib();
}

void OverlayWindow::ApplyClickThrough(bool clickThrough) {
    if (!m_hWnd) return;
    LONG_PTR ex = GetWindowLongPtr(m_hWnd, GWL_EXSTYLE);
    if (clickThrough) ex |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    else              ex &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    SetWindowLongPtr(m_hWnd, GWL_EXSTYLE, ex);
    SetWindowPos(m_hWnd, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

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

void OverlayWindow::HandleToggleVisible() {
    if (IsWindowVisible(m_hWnd)) {
        m_userHidden = true;
        ShowWindow(m_hWnd, SW_HIDE);
        return;
    }

    // Показывать вручную можно только когда целевое приложение в фокусе.
    if (!m_watcher.IsTargetForeground()) return;

    m_userHidden = false;
    m_watcher.RefreshMonitorRect(m_hWnd, this);
    ShowWindow(m_hWnd, SW_SHOWNOACTIVATE);
    UpdateDisplay();
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
    m_lastRotateClickImgIdx = -1;
    m_lastRotateClickTick = 0;
    UpdateDisplay();

    if (!m_editMode && m_layout.IsDirty()) {
        m_layout.SaveAll(m_watcher.GetTargets(), g_images);
    }
}

void OverlayWindow::HandleReloadImages() {
    if (!m_watcher.IsTargetForeground()) return;

    m_currentTargetProcess = m_watcher.ResolveCurrentTargetProcess();
    m_layout.SetOwner(m_currentTargetProcess);
    m_layout.LoadForCurrent(g_images);
    RunWithDialog([&]() { ReloadImagesKeepingState(); });
    m_layout.MarkDirty();
}

void OverlayWindow::HandleResetPos() {
    if (!m_watcher.IsTargetForeground()) return;
    RunWithDialog([&]() {
        int res = UiKit::ShowYesNo(m_hWnd, m_hInst,
            L"Reset layout",
            L"Reset all images to default position?\n\n"
            L"This will discard current layout and rotation.");
        if (res) {
            ConfigStore::ResetToDefaults(g_images);
            m_layout.MarkDirty();
            UpdateDisplay();
            m_layout.SaveAll(m_watcher.GetTargets(), g_images);
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
            UiKit::ShowWarning(m_hWnd, m_hInst, L"Capture foreground", msg);
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
            m_layout.SaveAll(targets, g_images);

            m_cfg.SaveLayoutForProcess(name, g_images, true);
            m_cfg.SaveHotkeysForProcess(name, ConfigStore::DefaultHotkeys(), true);

            m_currentTargetProcess = name;
            m_layout.SetOwner(m_currentTargetProcess);
            m_hotkeysMgr.ApplyForProcess(m_currentTargetProcess);
            m_layout.LoadForCurrent(g_images);

            m_watcher.RefreshMonitorRect(m_hWnd, this);
            ReloadImagesKeepingState();

            wchar_t msg[512];
            swprintf_s(msg, L"Added target process:\n%s\n\nTotal targets: %d",
                name, (int)targets.processes.size());
            UiKit::ShowInfo(m_hWnd, m_hInst, L"Capture foreground", msg);
        }
        else {
            wchar_t msg[512];
            swprintf_s(msg, L"This process is already in the list:\n%s", name);
            UiKit::ShowInfo(m_hWnd, m_hInst, L"Capture foreground", msg);
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
            UiKit::ShowInfo(m_hWnd, m_hInst, L"Remove foreground", msg);
            return;
        }
        if (targets.showEverywhere) {
            UiKit::ShowInfo(m_hWnd, m_hInst,
                L"Remove foreground",
                L"Overlay is currently shown for ANY window ('*' in config).\n"
                L"Nothing to remove from the target list.\n\n"
                L"Edit target_processes in the INI to disable this mode.");
            return;
        }
        targets.processes.erase(it);
        if (targets.processes.empty()) targets.showEverywhere = false;
        m_layout.SaveAll(targets, g_images);

        m_currentTargetProcess = m_watcher.ResolveCurrentTargetProcess();
        m_layout.SetOwner(m_currentTargetProcess);
        m_hotkeysMgr.ApplyForProcess(m_currentTargetProcess);
        m_layout.LoadForCurrent(g_images);
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
        UiKit::ShowInfo(m_hWnd, m_hInst, L"Remove foreground", msg);
        });
}

void OverlayWindow::HandleEditHotkeys() {
    bool currentStillRunning = false;
    if (!m_currentTargetProcess.empty()) {
        std::vector<std::wstring> running = m_watcher.GetRunningTargets();
        for (const auto& r : running) {
            if (_wcsicmp(r.c_str(), m_currentTargetProcess.c_str()) == 0) {
                currentStillRunning = true;
                break;
            }
        }
    }

    if (!currentStillRunning) {
        std::vector<std::wstring> running = m_watcher.GetRunningTargets();

        if (running.empty()) {
            int action = ProcessPicker::ShowNoProcesses(m_hWnd, m_hInst);
            if (action == 2) DestroyWindow(m_hWnd);
            return;
        }

        std::wstring chosenProc;

        if (!m_lastConfiguredProcess.empty()) {
            for (const auto& r : running) {
                if (_wcsicmp(r.c_str(), m_lastConfiguredProcess.c_str()) == 0) {
                    chosenProc = r;
                    break;
                }
            }
        }

        if (chosenProc.empty() && running.size() == 1)
            chosenProc = running[0];

        if (chosenProc.empty()) {
            if (!ProcessPicker::Show(m_hWnd, m_hInst, running, chosenProc))
                return;
        }

        if (!chosenProc.empty()) {
            DWORD targetPid = 0;
            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnap != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W pe = { 0 };
                pe.dwSize = sizeof(pe);
                if (Process32FirstW(hSnap, &pe)) {
                    do {
                        wchar_t low[MAX_PATH] = { 0 };
                        wcsncpy_s(low, pe.szExeFile, _TRUNCATE);
                        CharLowerBuffW(low, (DWORD)wcslen(low));
                        if (_wcsicmp(low, chosenProc.c_str()) == 0) {
                            targetPid = pe.th32ProcessID;
                            break;
                        }
                    } while (Process32NextW(hSnap, &pe));
                }
                CloseHandle(hSnap);
            }

            if (targetPid != 0) {
                HWND hGame = m_watcher.FindMainWindowForProcess(targetPid);
                if (hGame) {
                    if (IsIconic(hGame)) ShowWindow(hGame, SW_RESTORE);
                    SetForegroundWindow(hGame);
                    Sleep(150);
                }
                else {
                    UiKit::ShowInfo(m_hWnd, m_hInst,
                        L"Restore game",
                        L"Could not find a window for the selected process.\n\n"
                        L"The game might be minimized to tray.\n"
                        L"Restore it manually, then press '/' again.");
                    return;
                }
            }
        }

        m_currentTargetProcess = chosenProc;

        if (m_currentTargetProcess.empty()) {
            UiKit::ShowWarning(m_hWnd, m_hInst,
                L"Configuring hotkeys",
                L"Failed to determine the target process.");
            return;
        }

        m_layout.SetOwner(m_currentTargetProcess);
        m_hotkeysMgr.ApplyForProcess(m_currentTargetProcess);
        m_layout.LoadForCurrent(g_images);
    }

    m_lastConfiguredProcess = m_currentTargetProcess;

    RunWithDialog([&]() {
        m_hotkeysMgr.UnregisterAll();

        HotkeyConfig edited = m_hotkeysMgr.Current();
        bool exitRequested = false;
        bool saved = HotkeyEditorWindow::ShowModal(m_hWnd, m_hInst,
            edited, &exitRequested);

        if (exitRequested) {
            DestroyWindow(m_hWnd);
            return;
        }

        if (!saved) {
            m_hotkeysMgr.RegisterAll();
            return;
        }

        HotkeyConfig backup = m_hotkeysMgr.Current();
        m_hotkeysMgr.Mutable() = edited;

        m_hotkeysMgr.UnregisterAll();
        if (!m_hotkeysMgr.RegisterAll()) {
            m_hotkeysMgr.UnregisterAll();
            m_hotkeysMgr.Mutable() = backup;
            m_hotkeysMgr.RegisterAll();
            UiKit::ShowWarning(m_hWnd, m_hInst,
                L"Configuring hotkeys",
                L"Failed to register a new set of hotkeys.\n"
                L"Most likely, some keys are being used by other programs.\n\n"
                L"Changes have not been saved.");
            return;
        }

        m_hotkeysMgr.SaveForProcess(m_currentTargetProcess);

        wchar_t msg[256];
        swprintf_s(msg, L"Hotkeys saved for the process:\n%s",
            m_currentTargetProcess.c_str());
        UiKit::ShowInfo(m_hWnd, m_hInst,
            L"Configuring hotkeys", msg);
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
                if (!m_currentTargetProcess.empty() && m_layout.IsDirty()) {
                    if (!m_layout.SaveDirty(g_images)) canSwitch = false;
                }
                if (canSwitch) {
                    m_currentTargetProcess = newProc;
                    m_layout.SetOwner(m_currentTargetProcess);
                    m_hotkeysMgr.ApplyForProcess(m_currentTargetProcess);
                    m_layout.LoadForCurrent(g_images);

                    m_renderer.InvalidateResources();
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
            if (!m_editMode) m_currentTargetProcess.clear();
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

void OverlayWindow::HandleMouseWheel(WPARAM wParam, LPARAM lParam) {
    if (!m_editMode) return;
    int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    ScreenToClient(m_hWnd, &pt);
    for (int i = (int)g_images.size() - 1; i >= 0; --i) {
        if (!g_images[i].visible) continue;
        if (IsPointInImage(g_images[i], pt.x, pt.y)) {
            ZoomImage(g_images[i], delta);
            m_layout.MarkDirty();
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

    for (auto& img : g_images) {
        img.isDragging = img.isResizing = img.isRotating = false;
        img.resizeMode = RESIZE_NONE;
    }
    m_activeImgIdx = -1;
    if (GetCapture() == m_hWnd) ReleaseCapture();

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
                m_layout.MarkDirty();
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

    if (m_activeImgIdx >= 0 && m_activeImgIdx < (int)g_images.size()) {
        OverlayImage& im = g_images[m_activeImgIdx];
        if (im.isDragging) {
            im.x = x - im.dragOffsetX;
            im.y = y - im.dragOffsetY;
            m_layout.MarkDirty(); UpdateDisplay();
        }
        else if (im.isRotating) {
            UpdateRotate(im, pxX, pxY);
            m_layout.MarkDirty(); UpdateDisplay();
        }
        else if (im.isResizing) {
            switch (im.resizeMode) {
            case RESIZE_CORNER:      ResizeCorner(im, pxX, pxY);     break;
            case RESIZE_WIDTH_ONLY:  ResizeWidthOnly(im, pxX, pxY);  break;
            case RESIZE_HEIGHT_ONLY: ResizeHeightOnly(im, pxX, pxY); break;
            default: break;
            }
            m_layout.MarkDirty(); UpdateDisplay();
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
    for (int i = (int)g_images.size() - 1; i >= 0; --i) {
        OverlayImage& im = g_images[i];
        if (!im.pImage) continue;
        HitMask hm = ComputeHitMask(im, pxX, pxY);
        if (hm.inRotate) { SetCursor(LoadCursor(NULL, IDC_CROSS));    return; }
        if (hm.inResizeCorner) { SetCursor(LoadCursor(NULL, IDC_SIZENWSE)); return; }
        if (hm.inRightEdge) { SetCursor(LoadCursor(NULL, IDC_SIZEWE));   return; }
        if (hm.inBottomEdge) { SetCursor(LoadCursor(NULL, IDC_SIZENS));   return; }
        if (hm.inImage) { SetCursor(LoadCursor(NULL, IDC_SIZEALL));  return; }
    }
    SetCursor(LoadCursor(NULL, IDC_ARROW));
}

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
        m_renderer.ReleaseDib();
        m_renderer.InvalidateResources();
        m_watcher.RefreshMonitorRect(hWnd, this);
        UpdateDisplay();
        break;
    }

    case WM_APP_SHOW_OVERLAY:
        if (!m_watcher.IsTargetForeground()) break;
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
        break;

    case WM_LBUTTONUP:   HandleLButtonUp(); break;

    case WM_DESTROY:
        KillTimer(hWnd, TIMER_KEEP_TOPMOST);
        KillTimer(hWnd, TIMER_WATCH_GAME);
        m_layout.SaveAll(m_watcher.GetTargets(), g_images);
        m_hotkeysMgr.UnregisterAll();
        m_renderer.ReleaseDib();
        if (m_hdcScreen) { ReleaseDC(NULL, m_hdcScreen); m_hdcScreen = NULL; }
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}