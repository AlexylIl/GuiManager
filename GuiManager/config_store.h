#pragma once
#include "overlay.h"

#include <span>

struct HotkeyConfig {
    UINT toggle_visible = VK_F9;
    UINT toggle_edit = VK_F10;
    UINT reset_pos = VK_F11;
    UINT reload_images = VK_MULTIPLY;
    UINT capture_fg = VK_F8;
    UINT remove_fg = VK_F7;
    UINT edit_hotkeys = VK_DIVIDE;
};

struct HotkeyFieldDesc {
    const wchar_t* iniKey;
    const wchar_t* label;
    int            id;
    UINT HotkeyConfig::* member;
};

inline constexpr HotkeyFieldDesc kHotkeyFieldDescs[] = {
    { L"toggle_visible",     L"Show / hide overlay.",       HOTKEY_TOGGLE_VISIBLE, &HotkeyConfig::toggle_visible },
    { L"toggle_edit",        L"Edit mode.",                 HOTKEY_TOGGLE_EDIT,    &HotkeyConfig::toggle_edit },
    { L"reset_pos",          L"Resetting positions.",       HOTKEY_RESET_POS,      &HotkeyConfig::reset_pos },
    { L"reload_images",      L"Reload images.",             HOTKEY_RELOAD_IMAGES,  &HotkeyConfig::reload_images },
    { L"capture_foreground", L"Add the active process.",    HOTKEY_CAPTURE_FG,     &HotkeyConfig::capture_fg },
    { L"remove_foreground",  L"Delete the active process.", HOTKEY_REMOVE_FG,      &HotkeyConfig::remove_fg },
    { L"edit_hotkeys",       L"Editor Hotkeys.",            HOTKEY_EDIT_HOTKEYS,   &HotkeyConfig::edit_hotkeys },
};

inline constexpr int kHotkeyFieldDescCount =
(int)(sizeof(kHotkeyFieldDescs) / sizeof(kHotkeyFieldDescs[0]));

class ConfigStore {
public:
    ConfigStore() = default;
    ~ConfigStore() = default;

    void EnsureConfigExists() const;

    TargetPolicy LoadGlobalConfig() const;
    bool         SaveGlobalConfig(const TargetPolicy& targets,
        std::span<const HotkeyDef> hotkeys) const;

    static HotkeyConfig DefaultHotkeys();
    HotkeyConfig LoadHotkeysForProcess(const std::wstring& procName) const;
    bool         SaveHotkeysForProcess(const std::wstring& procName,
        const HotkeyConfig& hk,
        bool onlyIfEmpty) const;

    bool LoadLayoutForProcess(const std::wstring& procName,
        std::vector<OverlayImage>& images) const;
    bool SaveLayoutForProcess(const std::wstring& procName,
        const std::vector<OverlayImage>& images,
        bool onlyIfEmpty) const;

    bool SaveAll(const TargetPolicy& targets,
        std::span<const HotkeyDef> hotkeys,
        const std::wstring& layoutOwner,
        bool layoutDirty,
        const std::vector<OverlayImage>& images) const;

    static void ResetToDefaults(std::vector<OverlayImage>& images);

    bool GetConfigPath(wchar_t* out, DWORD cch) const;
    bool GetProcessConfigPath(const std::wstring& procName,
        wchar_t* out, DWORD cch, bool create) const;
    bool GetProcessDir(const std::wstring& procName,
        wchar_t* out, DWORD cch, bool create) const;
    bool GetProcessImagePath(const std::wstring& procName,
        const wchar_t* fileName,
        wchar_t* out, DWORD cch) const;

private:
    static std::wstring TrimAndLower(std::wstring s);
    static std::vector<std::wstring> ParseProcessList(const std::wstring& s);
    static int  ParseLayoutLine(const wchar_t* s, int& x, int& y, int& w, int& h,
        float& rot, int& vis);
    static std::wstring FormatHotkey(UINT vk);
    static UINT ParseVkHex(const wchar_t* s, UINT def);

    HotkeyConfig LoadGlobalHotkeys() const; 
};