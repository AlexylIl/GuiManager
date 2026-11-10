// ============================================================================
//  target_watcher.cpp
// ============================================================================
#include "overlay.h"
#include "target_watcher.h"
#include "overlay_window.h"

bool TargetWatcher::IsIgnoredProcess(const wchar_t* nameLower) {
    if (!nameLower || !nameLower[0]) return true;

    static const wchar_t* kIgnored[] = {
        L"explorer.exe",
        L"dwm.exe",
        L"csrss.exe",
        L"winlogon.exe",
        L"wininit.exe",
        L"services.exe",
        L"lsass.exe",
        L"smss.exe",
        L"svchost.exe",
        L"fontdrvhost.exe",
        L"ctfmon.exe",
        L"sihost.exe",
        L"taskhostw.exe",
        L"runtimebroker.exe",
        L"searchhost.exe",
        L"searchapp.exe",
        L"startmenuexperiencehost.exe",
        L"shellhost.exe",
        L"applicationframehost.exe",
        L"systemsettings.exe",
        L"textinputhost.exe",
        L"lockapp.exe",
        L"useroobebroker.exe",
        L"widgets.exe",
        L"widgetservice.exe",
        L"gamebar.exe",
        L"gamebarftserver.exe",
        L"gamebarpresencewriter.exe",
        L"xboxgamebarwidgets.exe",
        L"yourphone.exe",
        L"phoneexperiencehost.exe",
        L"nvidia share.exe",
        L"nvsphelper64.exe",
        L"amdow.exe",
        L"radeonsoftware.exe",
        L"igfxem.exe",
        L"igfxtray.exe",
        L"overlaywindowclass.exe",
    };
    for (auto p : kIgnored)
        if (wcscmp(nameLower, p) == 0) return true;
    return false;
}

void TargetWatcher::SetTargets(const TargetPolicy& t) { m_targets = t; }

bool TargetWatcher::GetProcessNameById(DWORD pid, wchar_t* outName, DWORD cch) {
    if (pid == 0) return false;
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return false;

    wchar_t path[MAX_PATH] = { 0 };
    DWORD size = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(hProc, 0, path, &size);
    CloseHandle(hProc);
    if (!ok) return false;

    const wchar_t* fileName = wcsrchr(path, L'\\');
    fileName = fileName ? fileName + 1 : path;
    wcsncpy_s(outName, cch, fileName, _TRUNCATE);
    if (outName[0]) CharLowerBuffW(outName, (DWORD)wcslen(outName));
    return true;
}

bool TargetWatcher::IsProcessInTargetList(const wchar_t* nameLower) const {
    if (m_targets.showEverywhere) return true;
    if (m_targets.processes.empty()) return false;
    for (auto& p : m_targets.processes)
        if (wcscmp(nameLower, p.c_str()) == 0) return true;
    return false;
}

TargetWatcher::ScanResult TargetWatcher::ScanTargets() const {
    ScanResult result;
    if (m_targets.showEverywhere) { result.anyRunning = true; return result; }
    if (m_targets.processes.empty()) return result;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32W pe = { 0 };
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            wchar_t low[MAX_PATH] = { 0 };
            wcsncpy_s(low, pe.szExeFile, _TRUNCATE);
            CharLowerBuffW(low, (DWORD)wcslen(low));
            if (IsIgnoredProcess(low)) continue;
            if (IsProcessInTargetList(low)) {
                result.anyRunning = true;
                if (result.firstFound.empty()) result.firstFound = low;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return result;
}

int TargetWatcher::CheckAnyTargetRunning() {
    if (m_targets.showEverywhere) return 1;
    if (m_targets.processes.empty()) return 0;
    return ScanTargets().anyRunning ? 1 : 0;
}

bool TargetWatcher::IsTargetForegroundCached() {
    HWND hFg = GetForegroundWindow();
    if (!hFg) return false;

    ULONGLONG now = GetTickCount64();
    if (m_fgCache.hwnd == hFg && m_fgCache.fgValidUntil > now)
        return m_fgCache.isTarget;

    DWORD pid = 0;
    GetWindowThreadProcessId(hFg, &pid);
    wchar_t proc[MAX_PATH] = { 0 };
    bool isTarget = false;
    if (GetProcessNameById(pid, proc, MAX_PATH) && !IsIgnoredProcess(proc))
        isTarget = IsProcessInTargetList(proc);

    m_fgCache.hwnd = hFg;
    m_fgCache.pid = pid;
    m_fgCache.isTarget = isTarget;
    m_fgCache.fgValidUntil = now + FG_CACHE_TTL_MS;
    return isTarget;
}

bool TargetWatcher::IsTargetForeground() {
    if (m_targets.showEverywhere) {
        HWND hFg = GetForegroundWindow();
        if (!hFg) return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(hFg, &pid);
        wchar_t proc[MAX_PATH] = { 0 };
        if (!GetProcessNameById(pid, proc, MAX_PATH)) return false;
        return !IsIgnoredProcess(proc);
    }
    if (m_targets.processes.empty()) return false;

    HWND hFg = GetForegroundWindow();
    if (!hFg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hFg, &pid);
    wchar_t proc[MAX_PATH] = { 0 };
    if (!GetProcessNameById(pid, proc, MAX_PATH)) return false;
    if (IsIgnoredProcess(proc)) return false;
    return IsProcessInTargetList(proc);
}

std::wstring TargetWatcher::ResolveCurrentTargetProcess() {
    HWND hFg = GetForegroundWindow();
    if (hFg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hFg, &pid);
        wchar_t name[MAX_PATH] = { 0 };
        if (GetProcessNameById(pid, name, MAX_PATH) &&
            !IsIgnoredProcess(name) &&
            IsProcessInTargetList(name))
        {
            return name;
        }
    }
    return std::wstring();
}

struct EnumCtxEx {
    TargetWatcher* watcher;
    HWND hwnd;
};

static BOOL CALLBACK EnumTargetProcEx(HWND h, LPARAM lp) {
    auto* c = reinterpret_cast<EnumCtxEx*>(lp);
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER) != NULL) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    wchar_t name[MAX_PATH] = { 0 };
    if (!TargetWatcher::GetProcessNameById(pid, name, MAX_PATH)) return TRUE;
    if (TargetWatcher::IsIgnoredProcess(name)) return TRUE;
    if (c->watcher->IsProcessInTargetList(name)) {
        c->hwnd = h;
        return FALSE;
    }
    return TRUE;
}

RECT TargetWatcher::ComputeTargetMonitorRect() {
    HWND target = NULL;
    HWND hFg = GetForegroundWindow();
    if (hFg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hFg, &pid);
        wchar_t name[MAX_PATH] = { 0 };
        if (GetProcessNameById(pid, name, MAX_PATH) &&
            IsProcessInTargetList(name))
            target = hFg;
    }
    if (!target && !m_targets.processes.empty()) {
        EnumCtxEx ctx = { this, NULL };
        EnumWindows(EnumTargetProcEx, reinterpret_cast<LPARAM>(&ctx));
        target = ctx.hwnd;
    }

    MONITORINFO mi = { sizeof(mi) };
    HMONITOR hMon = NULL;
    if (target) hMon = MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST);
    if (!hMon)  hMon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);

    RECT rc;
    if (hMon && GetMonitorInfoW(hMon, &mi)) rc = mi.rcMonitor;
    else {
        rc.left = 0; rc.top = 0;
        rc.right = GetSystemMetrics(SM_CXSCREEN);
        rc.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    return rc;
}

RECT TargetWatcher::GetTargetMonitorRect() {
    ULONGLONG now = GetTickCount64();
    HWND hFg = GetForegroundWindow();
    if (hFg && m_fgCache.monSource == hFg && m_fgCache.monValidUntil > now)
        return m_fgCache.monRect;

    if (hFg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hFg, &pid);
        wchar_t name[MAX_PATH] = { 0 };
        if (GetProcessNameById(pid, name, MAX_PATH) &&
            IsProcessInTargetList(name))
        {
            MONITORINFO mi = { sizeof(mi) };
            HMONITOR hMon = MonitorFromWindow(hFg, MONITOR_DEFAULTTONEAREST);
            RECT rc;
            if (hMon && GetMonitorInfoW(hMon, &mi)) rc = mi.rcMonitor;
            else {
                rc.left = 0; rc.top = 0;
                rc.right = GetSystemMetrics(SM_CXSCREEN);
                rc.bottom = GetSystemMetrics(SM_CYSCREEN);
            }
            m_fgCache.monSource = hFg;
            m_fgCache.monRect = rc;
            m_fgCache.monValidUntil = now + MON_CACHE_TTL_MS;
            return rc;
        }
    }

    RECT rc = ComputeTargetMonitorRect();
    m_fgCache.monSource = hFg;
    m_fgCache.monRect = rc;
    m_fgCache.monValidUntil = now + MON_CACHE_TTL_MS;
    return rc;
}

void TargetWatcher::RefreshMonitorRect(HWND hWnd, OverlayWindow* owner) {
    RECT r = GetTargetMonitorRect();
    if (r.right - r.left <= 0 || r.bottom - r.top <= 0) return;
    if (EqualRect(&r, &g_monRect)) return;
    g_monRect = r;
    if (hWnd) {
        SetWindowPos(hWnd, NULL, g_monRect.left, g_monRect.top,
            g_monRect.right - g_monRect.left,
            g_monRect.bottom - g_monRect.top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        if (owner) owner->InvalidateDibAndRedraw();
    }
}

void TargetWatcher::InvalidateCaches() {
    m_fgCache.monSource = NULL;
    m_fgCache.monValidUntil = 0;
    m_fgCache.hwnd = NULL;
    m_fgCache.fgValidUntil = 0;
}