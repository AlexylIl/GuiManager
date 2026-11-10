// ============================================================================
//  process_picker.cpp
// ============================================================================
#include <windows.h>
#include "ui_kit.h"
#include "process_picker.h"
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

// ============================================================================
//  Часть 1: выбор из списка запущенных процессов
// ============================================================================
namespace {

    const wchar_t* kPickerClass = L"ProcessPickerWindowClass";

    enum : int {
        IDC_LIST = 2001,
        IDC_OK = 2002,
        IDC_CANCEL = 2003,
    };

    struct PickerState {
        HWND hwnd = NULL;
        HWND hList = NULL;
        bool done = false;
        bool accepted = false;
        std::wstring selected;
        const std::vector<std::wstring>* items = nullptr;
        HINSTANCE hInst = nullptr;
    };

    PickerState* StateOf(HWND h) {
        return reinterpret_cast<PickerState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }

    void LayoutChildren(PickerState* s, int w, int h) {
        const int margin = 16;
        const int btnH = 32;
        const int btnW = 100;
        const int listY = 60;
        const int listH = h - listY - btnH - margin * 2;

        if (s->hList)
            MoveWindow(s->hList, margin, listY, w - margin * 2, listH, TRUE);

        int y = h - margin - btnH;
        int rightX = w - margin;
        if (HWND hOk = GetDlgItem(s->hwnd, IDC_OK))
            MoveWindow(hOk, rightX - btnW, y, btnW, btnH, TRUE);
        rightX -= btnW + 8;
        if (HWND hCancel = GetDlgItem(s->hwnd, IDC_CANCEL))
            MoveWindow(hCancel, rightX - btnW, y, btnW, btnH, TRUE);
    }

    void Accept(PickerState* s) {
        int sel = (int)SendMessageW(s->hList, LB_GETCURSEL, 0, 0);
        if (sel < 0) {
            UiKit::ShowWarning(s->hwnd, s->hInst,
                L"Select process",
                L"Please select a process from the list.");
            return;
        }
        wchar_t buf[MAX_PATH] = { 0 };
        SendMessageW(s->hList, LB_GETTEXT, (WPARAM)sel, (LPARAM)buf);
        s->selected = buf;
        s->accepted = true;
        s->done = true;
        DestroyWindow(s->hwnd);
    }

    void Cancel(PickerState* s) {
        s->accepted = false;
        s->done = true;
        DestroyWindow(s->hwnd);
    }

    LRESULT CALLBACK PickerWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        PickerState* s = StateOf(h);

        switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            s = reinterpret_cast<PickerState*>(cs->lpCreateParams);
            s->hwnd = h;
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));

            HINSTANCE hi = (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE);
            s->hInst = hi;

            CreateWindowExW(0, L"STATIC",
                L"Multiple target processes are running.\n"
                L"Select the one to configure:",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                16, 12, 420, 40, h, NULL, hi, NULL);

            s->hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                LBS_NOTIFY | LBS_HASSTRINGS,
                16, 60, 420, 100, h, (HMENU)(INT_PTR)IDC_LIST, hi, NULL);

            for (const auto& it : *s->items)
                SendMessageW(s->hList, LB_ADDSTRING, 0, (LPARAM)it.c_str());
            SendMessageW(s->hList, LB_SETCURSEL, 0, 0);

            UiKit::CreateButton(h, IDC_OK, L"OK",
                UiKit::ButtonStyle::Normal, 0, 0, 100, 32, hi);

            UiKit::CreateButton(h, IDC_CANCEL, L"Cancel",
                UiKit::ButtonStyle::Normal, 0, 0, 100, 32, hi);

            return 0;
        }

        case WM_SIZE:
            if (s) LayoutChildren(s, LOWORD(l), HIWORD(l));
            return 0;

        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(l);
            if (UiKit::DrawButton(dis)) return TRUE;
            break;
        }

        case WM_COMMAND: {
            int id = LOWORD(w);
            int code = HIWORD(w);
            if (id == IDC_OK) { if (s) Accept(s); return 0; }
            if (id == IDC_CANCEL) { if (s) Cancel(s); return 0; }
            if (id == IDC_LIST && code == LBN_DBLCLK) { if (s) Accept(s); return 0; }
            break;
        }

        case WM_KEYDOWN:
            if (w == VK_ESCAPE) { if (s) Cancel(s); return 0; }
            if (w == VK_RETURN) { if (s) Accept(s); return 0; }
            break;

        case WM_CLOSE:
            if (s) Cancel(s);
            return 0;

        case WM_DESTROY:
            if (s) s->done = true;
            return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }

} // namespace

bool ProcessPicker::Show(HWND owner, HINSTANCE hInst,
    const std::vector<std::wstring>& processes,
    std::wstring& outProc)
{
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &PickerWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(UiKit::Palette::kBg);
        wc.lpszClassName = kPickerClass;
        if (!RegisterClassExW(&wc)) {
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        }
        registered = true;
    }

    PickerState s;
    s.items = &processes;

    const int W = 480;
    const int H = 340;

    RECT ownerRect = { 0, 0, 0, 0 };
    if (owner && IsWindow(owner)) GetWindowRect(owner, &ownerRect);
    else {
        ownerRect.right = GetSystemMetrics(SM_CXSCREEN);
        ownerRect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - W) / 2;
    int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - H) / 2;

    HWND hWnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        kPickerClass, L"Select target process",
        WS_CAPTION | WS_SYSMENU | WS_POPUP,
        x, y, W, H, owner, NULL, hInst, &s);

    if (!hWnd) return false;

    if (owner) EnableWindow(owner, FALSE);

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);
    SetFocus(hWnd);

    MSG msg;
    while (!s.done && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (!IsDialogMessageW(hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (owner) EnableWindow(owner, TRUE);

    if (s.accepted) {
        outProc = s.selected;
        return true;
    }
    return false;
}

// ============================================================================
//  Часть 2: диалог «нет запущенных процессов» — через UiKit
// ============================================================================
int ProcessPicker::ShowNoProcesses(HWND owner, HINSTANCE hInst)
{
    UiKit::DialogButton bs[2] = {
        { L"OK, I'll start the game", UiKit::ButtonStyle::Normal, true },
        { L"Close Overlay",           UiKit::ButtonStyle::Danger, false },
    };
    UiKit::DialogSpec sp;
    sp.title = L"No target processes";
    sp.text =
        L"No target games are running.\n\n"
        L"To add a game, do one of the following:\n"
        L"  1. Start the game, Alt-Tab into it and press F8 -\n"
        L"     the overlay will add the active process as a target.\n"
        L"  2. Start the game, then press '/' again to configure\n"
        L"     hotkeys for it.\n\n"
        L"Or close the overlay if you don't need it now.";
    sp.buttons = bs;
    sp.buttonCount = 2;

    int r = UiKit::ShowDialog(owner, hInst, sp);
    if (r == 0) return 1;
    if (r == 1) return 2;
    return 0;
}