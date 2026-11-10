// ============================================================================
//  hotkey_manager.cpp
// ============================================================================
#include "overlay.h"
#include "hotkey_manager.h"

HotkeyManager::HotkeyManager(ConfigStore& cfg, HINSTANCE hInst)
    : m_store(cfg), m_hInst(hInst) {}

void HotkeyManager::ApplyForProcess(const std::wstring& proc) {
    UnregisterAll();
    m_cfg = m_store.LoadHotkeysForProcess(proc);
    RegisterAll();
}

bool HotkeyManager::RegisterAll() {
    if (!m_hWnd) return false;

    bool allOk = true;
    std::wstring warns;

    for (int i = 0; i < kHotkeyFieldDescCount; ++i) {
        const UINT vk = m_cfg.*(kHotkeyFieldDescs[i].member);
        if (!RegisterHotKey(m_hWnd, kHotkeyFieldDescs[i].id, MOD_NOREPEAT, vk)) {
            allOk = false;
            wchar_t line[128];
            swprintf_s(line, L"  %s vk=0x%X\n",
                kHotkeyFieldDescs[i].iniKey, vk);
            warns += line;
        }
    }

    if (!allOk) {
        OutputDebugStringW(L"[HotkeyManager] hotkey registration failed:\n");
        OutputDebugStringW(warns.c_str());
    }
    return allOk;
}

void HotkeyManager::UnregisterAll() {
    if (!m_hWnd) return;
    for (int i = 0; i < kHotkeyFieldDescCount; ++i)
        UnregisterHotKey(m_hWnd, kHotkeyFieldDescs[i].id);
}

bool HotkeyManager::SaveForProcess(const std::wstring& proc) const {
    return m_store.SaveHotkeysForProcess(proc, m_cfg, false);
}