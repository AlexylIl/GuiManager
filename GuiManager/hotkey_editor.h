// ============================================================================
//  hotkey_editor.h  — модальное окно редактирования хоткеев
// ============================================================================
#pragma once
#include "overlay.h"
#include "config_store.h"

class HotkeyEditorWindow {
public:
    static bool ShowModal(HWND owner, HINSTANCE hInst,
        HotkeyConfig& cfg,
        bool* outExitRequested = nullptr);
};