// ============================================================================
//  main.cpp  — точка входа
// ============================================================================
#include "overlay.h"
#include "ui_kit.h"
#include "config_store.h"
#include "image_manager.h"
#include "target_watcher.h"
#include "overlay_window.h"

ComPtr<IWICImagingFactory> g_wicFactory;
ComPtr<ID2D1Factory>       g_d2dFactory;
RECT  g_monRect = { 0, 0, 0, 0 };
UINT  g_currentDpi = 96;
std::vector<OverlayImage> g_images;

typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);

static void EnableDpiAwareness() {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        auto pfn = (PFN_SetProcessDpiAwarenessContext)
            GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pfn) { pfn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); return; }
    }
    SetProcessDPIAware();
}

static UINT GetSystemDpi() {
    HMONITOR hMon = MonitorFromRect(&g_monRect, MONITOR_DEFAULTTONEAREST);
    if (!hMon) hMon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);

    typedef HRESULT(WINAPI* PFN_GetDpiForMonitor)(HMONITOR, int, UINT*, UINT*);
    HMODULE hShcore = LoadLibraryW(L"shcore.dll");
    if (hShcore) {
        auto pfn = (PFN_GetDpiForMonitor)GetProcAddress(hShcore, "GetDpiForMonitor");
        if (pfn) {
            UINT dpiX = 96, dpiY = 96;
            if (SUCCEEDED(pfn(hMon, 0, &dpiX, &dpiY))) {
                FreeLibrary(hShcore);
                return dpiX ? dpiX : 96;
            }
        }
        FreeLibrary(hShcore);
    }
    HDC hdc = GetDC(NULL);
    UINT dpi = hdc ? (UINT)GetDeviceCaps(hdc, LOGPIXELSX) : 96;
    if (hdc) ReleaseDC(NULL, hdc);
    return dpi ? dpi : 96;
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    EnableDpiAwareness();

    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Local\\GameOverlay_SingleInstance_Mutex");
    if (hMutex == NULL) return 0;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowW(L"OverlayWindowClass", NULL);
        if (hExisting) PostMessageW(hExisting, WM_APP_SHOW_OVERLAY, 0, 0);
        CloseHandle(hMutex);
        return 0;
    }

    bool coInit = false;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (SUCCEEDED(hr)) coInit = true;
    else if (hr == RPC_E_CHANGED_MODE) coInit = false;
    else { CloseHandle(hMutex); return 0; }

    hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&g_wicFactory));
    if (FAILED(hr)) {
        if (coInit) CoUninitialize(); CloseHandle(hMutex); return 0;
    }

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory), nullptr,
        reinterpret_cast<void**>(g_d2dFactory.GetAddressOf()));
    if (FAILED(hr)) {
        g_wicFactory.Reset();
        if (coInit) CoUninitialize(); CloseHandle(hMutex); return 0;
    }

    ConfigStore  cfg;
    ImageManager im;
    TargetWatcher watcher;

    cfg.EnsureConfigExists();
    watcher.SetTargets(cfg.LoadGlobalConfig());

    for (auto& p : watcher.GetTargets().processes) {
        std::vector<OverlayImage> dummy;
        dummy.resize(MAX_IMAGES);
        cfg.SaveLayoutForProcess(p, dummy, /*onlyIfEmpty*/ true);
        cfg.SaveHotkeysForProcess(p, ConfigStore::DefaultHotkeys(),
            /*onlyIfEmpty*/ true);
    }

    std::wstring currentTarget = watcher.ResolveCurrentTargetProcess();

    g_images.clear();
    g_images.reserve(MAX_IMAGES);
    std::wstring loadDiag;
    int loadedCount = 0;
    for (int i = 0; i < MAX_IMAGES; ++i) {
        g_images.emplace_back();
        bool usedCustom = false;
        HRESULT lhr = im.LoadSlot(hInstance, i, currentTarget,
            g_images.back(), usedCustom, &loadDiag);
        if (SUCCEEDED(lhr)) ++loadedCount;
    }

    {
        g_monRect = watcher.ComputeTargetMonitorRect();
        g_currentDpi = GetSystemDpi();
    }

    OverlayWindow window(hInstance, cfg, im, watcher);

    window.SetCurrentTargetProcess(currentTarget);
    if (!window.Create()) {
        g_images.clear();
        g_d2dFactory.Reset(); g_wicFactory.Reset();
        if (coInit) CoUninitialize();
        ReleaseMutex(hMutex); CloseHandle(hMutex);
        return 0;
    }

    cfg.LoadLayoutForProcess(currentTarget, g_images);

    if (!loadDiag.empty()) {
        std::wstring msg = L"Some images failed to load:\n\n" + loadDiag;
        UiKit::ShowWarning(window.Handle(), hInstance,
            L"Image load warnings", msg.c_str());
    }
    else if (loadedCount == 0) {
        UiKit::ShowError(window.Handle(), hInstance,
            L"Resource load error",
            L"No images could be loaded.\n\n"
            L"Check overlay_imgN.png next to the .exe and the embedded "
            L"resources (RCDATA).");
    }

    window.InitialRender();

    SetTimer(window.Handle(), TIMER_KEEP_TOPMOST, 1000, NULL);
    SetTimer(window.Handle(), TIMER_WATCH_GAME, GAME_POLL_INTERVAL_MS, NULL);

    window.RunMessageLoop();

    g_images.clear();
    g_d2dFactory.Reset();
    g_wicFactory.Reset();
    if (coInit) CoUninitialize();
    if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
    return 0;
}