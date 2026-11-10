// ============================================================================
//  process_picker.h
// ============================================================================
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "overlay.h"

class ProcessPicker {
public:
    static bool Show(HWND owner, HINSTANCE hInst,
        const std::vector<std::wstring>& processes,
        std::wstring& outProc);
    static int ShowNoProcesses(HWND owner, HINSTANCE hInst);
};