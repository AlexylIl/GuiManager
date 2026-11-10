// ============================================================================
//  hotkey_manager.h  — регистрация/снятие/применение хоткеев
// ============================================================================
#pragma once
#include "overlay.h"
#include "config_store.h"

class HotkeyManager {
public:
    HotkeyManager(ConfigStore& cfg, HINSTANCE hInst);

    void SetWindow(HWND hwnd) { m_hWnd = hwnd; }

    void ApplyForProcess(const std::wstring& proc);

    bool RegisterAll();

    void UnregisterAll();

    const HotkeyConfig& Current() const { return m_cfg; }
    HotkeyConfig& Mutable() { return m_cfg; }

    bool SaveForProcess(const std::wstring& proc) const;

private:
    ConfigStore& m_store;
    HINSTANCE    m_hInst = NULL;
    HWND         m_hWnd = NULL;
    HotkeyConfig m_cfg;
};