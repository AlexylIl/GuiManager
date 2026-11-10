// ============================================================================
//  target_watcher.h
// ============================================================================
#pragma once
#include "overlay.h"

class TargetWatcher {
public:
    TargetWatcher() = default;


    static bool IsIgnoredProcess(const wchar_t* nameLower);

    void SetTargets(const TargetPolicy& t);
    const TargetPolicy& GetTargets() const { return m_targets; }
    TargetPolicy& MutableTargets() { return m_targets; }

    static bool GetProcessNameById(DWORD pid, wchar_t* outName, DWORD cch);

    bool BringTargetToForeground();
    bool IsProcessInTargetList(const wchar_t* nameLower) const;
    bool IsTargetForeground();
    bool IsTargetForegroundCached();
    int  CheckAnyTargetRunning();

    std::wstring ResolveCurrentTargetProcess();

    std::vector<std::wstring> GetRunningTargets() const;
    HWND FindMainWindowForProcess(DWORD pid) const;


    struct ScanResult { bool anyRunning = false; std::wstring firstFound; };
    ScanResult ScanTargets() const;

    RECT GetTargetMonitorRect();
    void RefreshMonitorRect(HWND hWnd, class OverlayWindow* owner);
    RECT ComputeTargetMonitorRect();

    void InvalidateCaches();

private:
    struct ForegroundCache {
        HWND      hwnd = NULL;
        DWORD     pid = 0;
        bool      isTarget = false;
        ULONGLONG fgValidUntil = 0;

        RECT      monRect = { 0, 0, 0, 0 };
        HWND      monSource = NULL;
        ULONGLONG monValidUntil = 0;
    };

    static const ULONGLONG FG_CACHE_TTL_MS = 200;
    static const ULONGLONG MON_CACHE_TTL_MS = 500;

    TargetPolicy    m_targets;
    ForegroundCache m_fgCache;
};