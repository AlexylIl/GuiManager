// ============================================================================
//  hotkey_editor.cpp
// ============================================================================
#include "overlay.h"
#include "hotkey_editor.h"

#include <string>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

namespace {

    const wchar_t* kClassName = L"HotkeyEditorWindowClass";

    // Control IDs
    enum : int {
        IDC_STATIC_BASE = 1000,
        IDC_BTN_BASE = 1100,
        IDC_BTN_SAVE = 1200,
        IDC_BTN_CANCEL = 1201,
        IDC_BTN_RESET = 1202,
        IDC_BTN_EXIT_APP = 1203,
        IDC_STATUS = 1300,
    };

    static const int kFieldCount = kHotkeyFieldDescCount;

    // Индексы кнопок в едином пространстве hoverBtn.
    // 0..kFieldCount-1  — field-кнопки
    // kFieldCount..     — нижний ряд
    enum : int {
        BTN_SAVE = kFieldCount,
        BTN_CANCEL,
        BTN_RESET,
        BTN_EXIT,
        BTN_COUNT
    };

    // ------------------------------------------------------------------
    //  Палитра (Вариант A: Win10-светлая)
    // ------------------------------------------------------------------
    namespace Palette {
        inline constexpr COLORREF kBtnBgNormal = RGB(250, 250, 250);
        inline constexpr COLORREF kBtnBgHover = RGB(229, 241, 251);
        inline constexpr COLORREF kBtnBgPressed = RGB(204, 228, 247);
        inline constexpr COLORREF kBtnBorderNormal = RGB(173, 173, 173);
        inline constexpr COLORREF kBtnBorderHover = RGB(0, 120, 215);
        inline constexpr COLORREF kBtnBorderPressed = RGB(0, 90, 158);
        inline constexpr COLORREF kBtnTextNormal = RGB(0, 0, 0);
        inline constexpr COLORREF kBtnTextDisabled = RGB(160, 160, 160);

        inline constexpr COLORREF kBtnBgCapturing = RGB(255, 244, 206);
        inline constexpr COLORREF kBtnBorderCapturing = RGB(255, 185, 0);
        inline constexpr COLORREF kBtnTextCapturing = RGB(122, 79, 0);

        inline constexpr COLORREF kExitNormal = RGB(200, 45, 45);
        inline constexpr COLORREF kExitHover = RGB(230, 57, 57);
        inline constexpr COLORREF kExitPressed = RGB(150, 30, 30);
        inline constexpr COLORREF kExitDisabled = RGB(150, 96, 96);
        inline constexpr COLORREF kExitText = RGB(255, 255, 255);
    }

    struct EditorState {
        HWND  hwnd = NULL;
        HINSTANCE hInst = NULL;
        HotkeyConfig cfg;
        HotkeyConfig original;
        bool  saved = false;
        bool  exitRequested = false;
        int   capturingIndex = -1;
        bool  running = true;
        HWND  hOwner = NULL;
        HWND  hBtn[kFieldCount] = {};
        HWND  hStatus = NULL;

        // hover-состояние (единое пространство BTN_*)
        int   hoverBtn = -1;

        // subclass-трекинг мыши на каждой кнопке
        bool  btnTrackingMouse[BTN_COUNT] = {};
    };

    EditorState* StateOf(HWND h) {
        return reinterpret_cast<EditorState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }

    // ------------------------------------------------------------------
    //  VK / валидация (без изменений от оригинала)
    // ------------------------------------------------------------------
    std::wstring VkToName(UINT vk) {
        if (vk == 0) return L"— (not set)";
        switch (vk) {
        case VK_F1:  return L"F1";  case VK_F2:  return L"F2";
        case VK_F3:  return L"F3";  case VK_F4:  return L"F4";
        case VK_F5:  return L"F5";  case VK_F6:  return L"F6";
        case VK_F7:  return L"F7";  case VK_F8:  return L"F8";
        case VK_F9:  return L"F9";  case VK_F10: return L"F10";
        case VK_F11: return L"F11"; case VK_F12: return L"F12";
        case VK_MULTIPLY: return L"Numpad *";
        case VK_DIVIDE:   return L"Numpad /";
        case VK_ADD:      return L"Numpad +";
        case VK_SUBTRACT: return L"Numpad -";
        case VK_OEM_MINUS:  return L"-";
        case VK_OEM_PLUS:   return L"=";
        case VK_OEM_1:      return L";";
        case VK_OEM_2:      return L"/";
        case VK_OEM_3:      return L"`";
        case VK_OEM_4:      return L"[";
        case VK_OEM_5:      return L"\\";
        case VK_OEM_6:      return L"]";
        case VK_OEM_7:      return L"'";
        case VK_OEM_COMMA:  return L",";
        case VK_OEM_PERIOD: return L".";
        case VK_HOME:   return L"Home";
        case VK_END:    return L"End";
        case VK_PRIOR:  return L"PgUp";
        case VK_NEXT:   return L"PgDn";
        case VK_INSERT: return L"Insert";
        case VK_DELETE: return L"Delete";
        case VK_TAB:    return L"Tab";
        case VK_SPACE:  return L"Space";
        default: {
            wchar_t buf[32];
            swprintf_s(buf, L"0x%X", vk);
            return buf;
        }
        }
    }

    bool IsForbiddenVk(UINT vk) {
        switch (vk) {
        case VK_SHIFT: case VK_CONTROL: case VK_MENU:
        case VK_LWIN:  case VK_RWIN:
        case VK_LSHIFT: case VK_RSHIFT:
        case VK_LCONTROL: case VK_RCONTROL:
        case VK_LMENU: case VK_RMENU:
        case VK_ESCAPE:
        case 0:
            return true;
        default:
            return false;
        }
    }

    void RefreshButtons(EditorState* s) {
        for (int i = 0; i < kFieldCount; ++i) {
            std::wstring text;
            if (s->capturingIndex == i) {
                text = L"Press a key... (Esc - Cancel)";
            }
            else {
                UINT vk = s->cfg.*(kHotkeyFieldDescs[i].member);
                if (vk == 0)
                    text = L"[ !!! Not set ]";
                else
                    text = L"[ " + VkToName(vk) + L" ]";
            }
            SetWindowTextW(s->hBtn[i], text.c_str());
            InvalidateRect(s->hBtn[i], NULL, FALSE);
        }
        if (s->hStatus) {
            SetWindowTextW(s->hStatus,
                s->capturingIndex >= 0
                ? L"Waiting for a key..."
                : L"Select an action and press the key on the right.");
        }
    }

    void ApplyNewVk(EditorState* s, UINT vk) {
        if (s->capturingIndex < 0) return;
        for (int i = 0; i < kFieldCount; ++i) {
            if (i == s->capturingIndex) continue;
            UINT& other = s->cfg.*(kHotkeyFieldDescs[i].member);
            if (other == vk) other = 0;
        }
        s->cfg.*(kHotkeyFieldDescs[s->capturingIndex].member) = vk;
        s->capturingIndex = -1;
        RefreshButtons(s);
    }

    // ------------------------------------------------------------------
    //  Рисование кнопок
    // ------------------------------------------------------------------
    void FillAndFrame(HDC dc, const RECT& rc, COLORREF bg,
        COLORREF border, int borderWidth)
    {
        HBRUSH br = CreateSolidBrush(bg);
        FillRect(dc, &rc, br);
        DeleteObject(br);

        HPEN pen = CreatePen(PS_SOLID, borderWidth, border);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBr = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBr);
        DeleteObject(pen);
    }

    void DrawTextInButton(HDC dc, const RECT& rc, const wchar_t* text,
        COLORREF color, bool offsetByPress)
    {
        RECT tr = rc;
        if (offsetByPress) OffsetRect(&tr, 1, 1);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);

        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HGDIOBJ oldFont = SelectObject(dc, font);
        DrawTextW(dc, text, -1, &tr,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, oldFont);
    }

    // Рисуем обычную кнопку (field, Save, Cancel, Reset).
    // index — в едином пространстве BTN_* (для field: 0..kFieldCount-1).
    void DrawButton(const DRAWITEMSTRUCT* dis, EditorState* s, int index) {
        HDC dc = dis->hDC;
        RECT rc = dis->rcItem;

        bool disabled = (dis->itemState & ODS_DISABLED) != 0;
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        bool focused = (dis->itemState & ODS_FOCUS) != 0;
        bool capturing = (index >= 0 && index < kFieldCount &&
            s->capturingIndex == index);
        bool hover = (s->hoverBtn == index);

        COLORREF bg, border, textColor;
        int borderWidth = 1;

        if (disabled) {
            bg = Palette::kBtnBgNormal;
            border = Palette::kBtnBorderNormal;
            textColor = Palette::kBtnTextDisabled;
        }
        else if (capturing) {
            bg = Palette::kBtnBgCapturing;
            border = Palette::kBtnBorderCapturing;
            textColor = Palette::kBtnTextCapturing;
            borderWidth = 2;
        }
        else if (pressed) {
            bg = Palette::kBtnBgPressed;
            border = Palette::kBtnBorderPressed;
            textColor = Palette::kBtnTextNormal;
            borderWidth = 2;
        }
        else if (hover) {
            bg = Palette::kBtnBgHover;
            border = Palette::kBtnBorderHover;
            textColor = Palette::kBtnTextNormal;
            borderWidth = 2;
        }
        else {
            bg = Palette::kBtnBgNormal;
            border = Palette::kBtnBorderNormal;
            textColor = Palette::kBtnTextNormal;
        }

        FillAndFrame(dc, rc, bg, border, borderWidth);

        wchar_t text[128] = { 0 };
        GetWindowTextW(dis->hwndItem, text, 128);
        DrawTextInButton(dc, rc, text, textColor, pressed && !disabled);

        if (focused && !disabled) {
            RECT fr = rc;
            InflateRect(&fr, -3, -3);
            DrawFocusRect(dc, &fr);
        }
    }

    // Exit — отдельная палитра, но те же состояния.
    void DrawExitButton(const DRAWITEMSTRUCT* dis, EditorState* s) {
        HDC dc = dis->hDC;
        RECT rc = dis->rcItem;

        bool disabled = (dis->itemState & ODS_DISABLED) != 0;
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        bool focused = (dis->itemState & ODS_FOCUS) != 0;
        bool hover = (s->hoverBtn == BTN_EXIT);

        COLORREF bg;
        if (disabled)     bg = Palette::kExitDisabled;
        else if (pressed) bg = Palette::kExitPressed;
        else if (hover)   bg = Palette::kExitHover;
        else              bg = Palette::kExitNormal;

        FillAndFrame(dc, rc, bg, RGB(0, 0, 0), 1);

        wchar_t text[64] = { 0 };
        GetWindowTextW(dis->hwndItem, text, 64);
        DrawTextInButton(dc, rc, text, Palette::kExitText,
            pressed && !disabled);

        if (focused && !disabled) {
            RECT fr = rc;
            InflateRect(&fr, -3, -3);
            DrawFocusRect(dc, &fr);
        }
    }

    // ------------------------------------------------------------------
    //  Layout (без изменений)
    // ------------------------------------------------------------------
    void LayoutChildren(EditorState* s, int w, int h) {
        const int margin = 16;
        const int rowH = 32;
        const int btnW = 220;
        const int labelW = w - margin * 2 - btnW - 16;

        int y = margin;
        for (int i = 0; i < kFieldCount; ++i) {
            HWND lbl = GetDlgItem(s->hwnd, IDC_STATIC_BASE + i);
            HWND btn = s->hBtn[i];
            if (lbl) MoveWindow(lbl, margin, y + 8, labelW, 20, FALSE);
            if (btn) MoveWindow(btn, margin + labelW + 16, y, btnW, rowH, TRUE);
            y += rowH + 6;
        }

        int bottomY = h - margin - 32;
        int exitW = 100, resetW = 120, saveW = 120, cancelW = 100;

        HWND hExit = GetDlgItem(s->hwnd, IDC_BTN_EXIT_APP);
        HWND hReset = GetDlgItem(s->hwnd, IDC_BTN_RESET);
        HWND hSave = GetDlgItem(s->hwnd, IDC_BTN_SAVE);
        HWND hCancel = GetDlgItem(s->hwnd, IDC_BTN_CANCEL);

        if (s->hStatus)
            MoveWindow(s->hStatus, margin + exitW + 16, bottomY - 28,
                w - margin * 2 - exitW - 16, 20, FALSE);

        int rightX = w - margin;
        if (hSave)   MoveWindow(hSave, rightX - saveW, bottomY, saveW, 32, TRUE);
        rightX -= saveW + 8;
        if (hCancel) MoveWindow(hCancel, rightX - cancelW, bottomY, cancelW, 32, TRUE);
        rightX -= cancelW + 8;
        if (hReset)  MoveWindow(hReset, rightX - resetW, bottomY, resetW, 32, TRUE);

        if (hExit)   MoveWindow(hExit, margin, bottomY, exitW, 32, TRUE);
    }

    bool ValidateConfig(EditorState* s, std::wstring& errOut) {
        for (int i = 0; i < kFieldCount; ++i) {
            UINT a = s->cfg.*(kHotkeyFieldDescs[i].member);
            if (a == 0) {
                errOut = L"Key not assigned for \""
                    + std::wstring(kHotkeyFieldDescs[i].label)
                    + L"\".\n\nAssign a key to every action before saving.";
                return false;
            }
            if (IsForbiddenVk(a)) {
                errOut = L"Invalid key for \"" + std::wstring(kHotkeyFieldDescs[i].label)
                    + L"\": " + VkToName(a);
                return false;
            }
            for (int j = i + 1; j < kFieldCount; ++j) {
                UINT b = s->cfg.*(kHotkeyFieldDescs[j].member);
                if (a == b) {
                    errOut = L"The key " + VkToName(a)
                        + L" is assigned twice:\n\""
                        + kHotkeyFieldDescs[i].label + L"\" and \""
                        + kHotkeyFieldDescs[j].label + L"\"";
                    return false;
                }
            }
        }
        return true;
    }

    void OnSave(EditorState* s) {
        std::wstring err;
        if (!ValidateConfig(s, err)) {
            MessageBoxW(s->hwnd, err.c_str(), L"Error", MB_OK | MB_ICONWARNING);
            return;
        }
        s->saved = true;
        s->running = false;
        DestroyWindow(s->hwnd);
    }

    void OnCancel(EditorState* s) {
        s->cfg = s->original;
        s->saved = false;
        s->running = false;
        DestroyWindow(s->hwnd);
    }

    void OnReset(EditorState* s) {
        int res = MessageBoxW(s->hwnd,
            L"Reset all hotkeys to defaults?\n\n"
            L"This will discard your current changes (they are not saved yet).",
            L"Reset hotkeys", MB_YESNO | MB_ICONQUESTION);
        if (res != IDYES) return;
        s->cfg = HotkeyConfig{};
        s->capturingIndex = -1;
        RefreshButtons(s);
    }

    void OnExitApp(EditorState* s) {
        int res = MessageBoxW(s->hwnd,
            L"Exit the overlay application?\n\n"
            L"Unsaved hotkey changes will be lost.",
            L"Exit overlay", MB_YESNO | MB_ICONWARNING);
        if (res != IDYES) return;
        s->saved = false;
        s->exitRequested = true;
        s->running = false;
        DestroyWindow(s->hwnd);
    }

    // ------------------------------------------------------------------
    //  Subclass для hover-трекинга каждой кнопки
    // ------------------------------------------------------------------
    LRESULT CALLBACK ButtonSubclassProc(HWND h, UINT msg, WPARAM w, LPARAM l,
        UINT_PTR /*id*/, DWORD_PTR ref)
    {
        EditorState* s = reinterpret_cast<EditorState*>(ref);
        if (!s) return DefSubclassProc(h, msg, w, l);

        // Определяем индекс кнопки в едином пространстве
        int ctrlId = GetDlgCtrlID(h);
        int index = -1;
        if (ctrlId >= IDC_BTN_BASE && ctrlId < IDC_BTN_BASE + kFieldCount)
            index = ctrlId - IDC_BTN_BASE;
        else if (ctrlId == IDC_BTN_SAVE)   index = BTN_SAVE;
        else if (ctrlId == IDC_BTN_CANCEL) index = BTN_CANCEL;
        else if (ctrlId == IDC_BTN_RESET)  index = BTN_RESET;
        else if (ctrlId == IDC_BTN_EXIT_APP) index = BTN_EXIT;

        if (index < 0) return DefSubclassProc(h, msg, w, l);

        switch (msg) {
        case WM_MOUSEMOVE: {
            if (!s->btnTrackingMouse[index]) {
                TRACKMOUSEEVENT tme = { sizeof(tme) };
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = h;
                TrackMouseEvent(&tme);
                s->btnTrackingMouse[index] = true;
            }
            if (s->hoverBtn != index) {
                s->hoverBtn = index;
                InvalidateRect(h, NULL, FALSE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            s->btnTrackingMouse[index] = false;
            if (s->hoverBtn == index) {
                s->hoverBtn = -1;
                InvalidateRect(h, NULL, FALSE);
            }
            break;
        }
        case WM_SETCURSOR: {
            if (LOWORD(l) == HTCLIENT) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                return TRUE;
            }
            break;
        }
        case WM_LBUTTONUP: {
            // Мышь ушла с кнопки, но клик отпущен здесь — гарантируем,
            // что BS_OWNERDRAW перерисует normal-состояние.
            // Реальную команду WM_COMMAND обработает DefSubclassProc.
            break;
        }
        }

        return DefSubclassProc(h, msg, w, l);
    }

    // ------------------------------------------------------------------
    //  WndProc
    // ------------------------------------------------------------------
    LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        EditorState* s = StateOf(h);

        switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            s = reinterpret_cast<EditorState*>(cs->lpCreateParams);
            s->hwnd = h;
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));

            HINSTANCE hi = (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE);
            s->hInst = hi;

            for (int i = 0; i < kFieldCount; ++i) {
                CreateWindowExW(0, L"STATIC", kHotkeyFieldDescs[i].label,
                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                    0, 0, 10, 10, h, (HMENU)(INT_PTR)(IDC_STATIC_BASE + i), hi, NULL);

                s->hBtn[i] = CreateWindowExW(0, L"BUTTON", L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    0, 0, 10, 10, h, (HMENU)(INT_PTR)(IDC_BTN_BASE + i), hi, NULL);
                SetWindowSubclass(s->hBtn[i], ButtonSubclassProc,
                    (UINT_PTR)(IDC_BTN_BASE + i), (DWORD_PTR)s);
            }

            s->hStatus = CreateWindowExW(0, L"STATIC", L"",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_STATUS, hi, NULL);

            HWND hExit = CreateWindowExW(0, L"BUTTON", L"Exit",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_BTN_EXIT_APP, hi, NULL);
            SetWindowSubclass(hExit, ButtonSubclassProc,
                (UINT_PTR)IDC_BTN_EXIT_APP, (DWORD_PTR)s);

            HWND hReset = CreateWindowExW(0, L"BUTTON", L"Reset",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_BTN_RESET, hi, NULL);
            SetWindowSubclass(hReset, ButtonSubclassProc,
                (UINT_PTR)IDC_BTN_RESET, (DWORD_PTR)s);

            HWND hSave = CreateWindowExW(0, L"BUTTON", L"Save",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
                0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_BTN_SAVE, hi, NULL);
            SetWindowSubclass(hSave, ButtonSubclassProc,
                (UINT_PTR)IDC_BTN_SAVE, (DWORD_PTR)s);

            HWND hCancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_BTN_CANCEL, hi, NULL);
            SetWindowSubclass(hCancel, ButtonSubclassProc,
                (UINT_PTR)IDC_BTN_CANCEL, (DWORD_PTR)s);

            RefreshButtons(s);
            return 0;
        }

        case WM_SIZE:
            if (s) LayoutChildren(s, LOWORD(l), HIWORD(l));
            return 0;

        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(l);
            if (!dis || !s) break;

            if (dis->CtlID == IDC_BTN_EXIT_APP) {
                DrawExitButton(dis, s);
                return TRUE;
            }
            if (dis->CtlID >= IDC_BTN_BASE && dis->CtlID < IDC_BTN_BASE + kFieldCount) {
                DrawButton(dis, s, dis->CtlID - IDC_BTN_BASE);
                return TRUE;
            }
            if (dis->CtlID == IDC_BTN_SAVE) { DrawButton(dis, s, BTN_SAVE);   return TRUE; }
            if (dis->CtlID == IDC_BTN_CANCEL) { DrawButton(dis, s, BTN_CANCEL); return TRUE; }
            if (dis->CtlID == IDC_BTN_RESET) { DrawButton(dis, s, BTN_RESET);  return TRUE; }
            break;
        }

        case WM_COMMAND: {
            if (!s) break;
            int id = LOWORD(w);
            if (id >= IDC_BTN_BASE && id < IDC_BTN_BASE + kFieldCount) {
                int idx = id - IDC_BTN_BASE;
                s->capturingIndex = idx;
                SetFocus(h);
                RefreshButtons(s);
                return 0;
            }
            if (id == IDC_BTN_SAVE) { OnSave(s);    return 0; }
            if (id == IDC_BTN_CANCEL) { OnCancel(s);  return 0; }
            if (id == IDC_BTN_RESET) { OnReset(s);   return 0; }
            if (id == IDC_BTN_EXIT_APP) { OnExitApp(s); return 0; }
            break;
        }

        case WM_KEYDOWN: {
            if (!s) break;
            if (s->capturingIndex >= 0) {
                UINT vk = (UINT)w;
                if (vk == VK_ESCAPE) {
                    s->capturingIndex = -1;
                    RefreshButtons(s);
                    return 0;
                }
                if (IsForbiddenVk(vk)) {
                    MessageBoxW(h,
                        L"This key cannot be used as a hotkey "
                        L"(modifier, Esc, or invalid code).",
                        L"Invalid key", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                ApplyNewVk(s, vk);
                return 0;
            }
            if ((UINT)w == VK_ESCAPE) { OnCancel(s); return 0; }
            if ((UINT)w == VK_RETURN) { OnSave(s);   return 0; }
            break;
        }

        case WM_SYSKEYDOWN: {
            if (!s) break;
            if (s->capturingIndex >= 0) {
                UINT vk = (UINT)w;
                if (vk == VK_ESCAPE) {
                    s->capturingIndex = -1;
                    RefreshButtons(s);
                    return 0;
                }
                if (IsForbiddenVk(vk)) {
                    MessageBoxW(h,
                        L"This key cannot be used as a hotkey "
                        L"(modifier, Esc, or invalid code).",
                        L"Invalid key", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                ApplyNewVk(s, vk);
                return 0;
            }
            break;
        }

        case WM_CLOSE:
            if (s) OnCancel(s);
            return 0;

        case WM_DESTROY:
            if (s) s->running = false;
            return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }

} // namespace

// ============================================================================
//  ShowModal
// ============================================================================
bool HotkeyEditorWindow::ShowModal(HWND owner, HINSTANCE hInst,
    HotkeyConfig& cfg,
    bool* outExitRequested)
{
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &WndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kClassName;
        wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_ICON1));
        wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1),
            IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
        if (!RegisterClassExW(&wc)) {
            DWORD err = GetLastError();
            if (err != ERROR_CLASS_ALREADY_EXISTS) return false;
        }
        classRegistered = true;
    }

    EditorState s;
    s.original = cfg;
    s.cfg = cfg;
    s.hOwner = owner;

    const int W = 640;
    const int H = 16 + kFieldCount * 38 + 100;

    RECT ownerRect = { 0, 0, 0, 0 };
    if (owner && IsWindow(owner)) GetWindowRect(owner, &ownerRect);
    else {
        ownerRect.right = GetSystemMetrics(SM_CXSCREEN);
        ownerRect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - W) / 2;
    int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - H) / 2;

    HWND hWnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,   // <-- CONTROLPARENT для IsDialogMessage
        kClassName, L"Configuring hotkeys",
        WS_CAPTION | WS_SYSMENU | WS_POPUP,
        x, y, W, H,
        owner, NULL, hInst, &s);

    if (!hWnd) return false;

    if (owner) EnableWindow(owner, FALSE);

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);
    SetFocus(hWnd);

    MSG msg;
    while (s.running && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (!IsDialogMessageW(hWnd, &msg)) {   // <-- Tab/Space/Enter
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (owner) EnableWindow(owner, TRUE);

    if (outExitRequested) *outExitRequested = s.exitRequested;
    if (s.saved) cfg = s.cfg;
    return s.saved;
}