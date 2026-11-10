#include "ui_kit.h"
#include <commctrl.h>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace UiKit {

    namespace {

        struct BtnState {
            bool hover = false;
            bool tracking = false;
        };

        LRESULT CALLBACK BtnSubclass(HWND h, UINT msg, WPARAM w, LPARAM l,
            UINT_PTR /*id*/, DWORD_PTR ref)
        {
            auto* st = reinterpret_cast<BtnState*>(ref);
            if (!st) return DefSubclassProc(h, msg, w, l);

            switch (msg) {
            case WM_MOUSEMOVE:
                if (!st->tracking) {
                    TRACKMOUSEEVENT tme = { sizeof(tme) };
                    tme.dwFlags = TME_LEAVE;
                    tme.hwndTrack = h;
                    TrackMouseEvent(&tme);
                    st->tracking = true;
                }
                if (!st->hover) { st->hover = true; InvalidateRect(h, NULL, FALSE); }
                break;
            case WM_MOUSELEAVE:
                st->tracking = false;
                st->hover = false;
                InvalidateRect(h, NULL, FALSE);
                break;
            case WM_SETCURSOR:
                if (LOWORD(l) == HTCLIENT) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }
                break;
            case WM_NCDESTROY:
                RemoveWindowSubclass(h, BtnSubclass, (UINT_PTR)GetDlgCtrlID(h));
                delete st;
                break;
            }
            return DefSubclassProc(h, msg, w, l);
        }

        void FillAndFrame(HDC dc, const RECT& rc, COLORREF bg,
            COLORREF border, int bw)
        {
            HBRUSH br = CreateSolidBrush(bg);
            FillRect(dc, &rc, br);
            DeleteObject(br);

            HPEN pen = CreatePen(PS_SOLID, bw, border);
            HGDIOBJ op = SelectObject(dc, pen);
            HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(dc, op);
            SelectObject(dc, ob);
            DeleteObject(pen);
        }

        void DrawTextInRect(HDC dc, const RECT& rc, const wchar_t* text,
            COLORREF color, bool offsetByPress)
        {
            RECT tr = rc;
            if (offsetByPress) OffsetRect(&tr, 1, 1);

            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, color);

            HFONT font = CreateFontW(
                -14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            HGDIOBJ of = SelectObject(dc, font);

            DrawTextW(dc, text, -1, &tr,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            SelectObject(dc, of);
            DeleteObject(font);
        }

        SIZE MeasureMultiline(const wchar_t* text, int width)
        {
            SIZE sz = { 0, 0 };
            if (!text || !text[0]) return sz;

            HDC dc = GetDC(NULL);
            if (!dc) return sz;

            NONCLIENTMETRICSW ncm = { sizeof(ncm) };
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);

            if (ncm.lfMessageFont.lfHeight < 0) ncm.lfMessageFont.lfHeight -= 4;
            else ncm.lfMessageFont.lfHeight += 4;

            HFONT font = CreateFontIndirectW(&ncm.lfMessageFont);
            HGDIOBJ oldFont = SelectObject(dc, font);

            int calcWidth = width - 8;
            if (calcWidth < 10) calcWidth = 10;

            RECT rc = { 0, 0, calcWidth, 0 };
            DrawTextW(dc, text, -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);

            sz.cx = rc.right - rc.left;
            sz.cy = (rc.bottom - rc.top) + 12;

            SelectObject(dc, oldFont);
            DeleteObject(font);
            ReleaseDC(NULL, dc);
            return sz;
        }

        SIZE MeasureSingleline(const wchar_t* text)
        {
            SIZE sz = { 0, 0 };
            if (!text || !text[0]) return sz;

            HDC dc = GetDC(NULL);
            if (!dc) return sz;

            HFONT font = CreateFontW(
                -14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            HGDIOBJ oldFont = SelectObject(dc, font);

            RECT rc = { 0, 0, 0, 0 };
            DrawTextW(dc, text, -1, &rc, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
            sz.cx = rc.right - rc.left;
            sz.cy = rc.bottom - rc.top;

            SelectObject(dc, oldFont);
            DeleteObject(font);
            ReleaseDC(NULL, dc);
            return sz;
        }

    }

    HWND CreateButton(HWND parent, int id, const wchar_t* text,
        ButtonStyle style, int x, int y, int w, int h,
        HINSTANCE hInst)
    {
        HWND hw = CreateWindowExW(0, L"BUTTON", text ? text : L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            x, y, w, h, parent, (HMENU)(INT_PTR)id, hInst, NULL);
        if (!hw) return NULL;

        auto* st = new BtnState();
        SetWindowSubclass(hw, BtnSubclass, (UINT_PTR)id, (DWORD_PTR)st);
        SetPropW(hw, L"uikit_style", (HANDLE)(INT_PTR)style);

        return hw;
    }

    bool DrawButton(const DRAWITEMSTRUCT* dis)
    {
        if (!dis || dis->CtlType != ODT_BUTTON) return false;

        ButtonStyle style = ButtonStyle::Normal;
        if (HANDLE p = GetPropW(dis->hwndItem, L"uikit_style"))
            style = (ButtonStyle)(INT_PTR)p;

        bool isDefault = GetPropW(dis->hwndItem, L"uikit_default") != NULL;

        bool disabled = (dis->itemState & ODS_DISABLED) != 0;
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        bool focused = (dis->itemState & ODS_FOCUS) != 0;

        bool hover = false;
        POINT pt;
        if (GetCursorPos(&pt)) {
            RECT wr; GetWindowRect(dis->hwndItem, &wr);
            hover = PtInRect(&wr, pt) != FALSE;
        }

        COLORREF bg, border, textColor;
        int bw = 1;

        if (style == ButtonStyle::Danger) {
            if (disabled)     bg = RGB(150, 96, 96);
            else if (pressed) bg = Palette::kDangerPressed;
            else if (hover)   bg = Palette::kDangerHover;
            else              bg = Palette::kDangerNormal;
            border = RGB(0, 0, 0);
            textColor = Palette::kDangerText;
            if (isDefault && !disabled && !pressed) {
                border = RGB(140, 20, 20);
                bw = 2;
            }
        }
        else {
            if (disabled) {
                bg = Palette::kBtnBgNormal;
                border = Palette::kBtnBorderNormal;
                textColor = Palette::kBtnTextDisabled;
            }
            else if (pressed) {
                bg = Palette::kBtnBgPressed;
                border = Palette::kBtnBorderPressed;
                textColor = Palette::kBtnTextNormal;
                bw = 2;
            }
            else if (hover) {
                bg = Palette::kBtnBgHover;
                border = Palette::kBtnBorderHover;
                textColor = Palette::kBtnTextNormal;
                bw = 2;
            }
            else {
                bg = Palette::kBtnBgNormal;
                border = isDefault ? RGB(130, 160, 200)
                    : Palette::kBtnBorderNormal;
                textColor = Palette::kBtnTextNormal;
                if (isDefault) bw = 2;
            }
        }

        RECT rc = dis->rcItem;
        FillAndFrame(dis->hDC, rc, bg, border, bw);

        wchar_t text[256] = { 0 };
        GetWindowTextW(dis->hwndItem, text, 256);
        DrawTextInRect(dis->hDC, rc, text, textColor, pressed && !disabled);

        if (focused && !disabled) {
            RECT fr = rc;
            InflateRect(&fr, -3, -3);
            DrawFocusRect(dis->hDC, &fr);
        }
        return true;
    }

    namespace {

        const wchar_t* kDialogClass = L"UiKitDialogClass";

        enum : int {
            IDC_DLG_ICON = 4001,
            IDC_DLG_TEXT = 4002,
            IDC_DLG_BTN = 4100,
        };

        struct DialogState {
            HWND hwnd = NULL;
            HINSTANCE hInst = NULL;
            bool done = false;
            int  result = -1;
            DialogSpec spec;
            std::vector<HWND> btnHwnds;
            HFONT hFont = NULL;
        };

        DialogState* StateOf(HWND h) {
            return reinterpret_cast<DialogState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }

        void LayoutDialog(DialogState* s, int w, int h) {
            const int margin = 20;
            const int iconSz = 48;
            const int btnH = 34;
            const int btnMinW = 110;
            const int btnPadW = 40;
            const int btnGap = 10;

            int iconX = margin;
            int iconY = margin;
            int textX = margin + (s->spec.iconResId ? iconSz + 16 : 0);
            int textW = w - textX - margin;
            int textY = margin;

            if (s->spec.iconResId) {
                HWND hIcon = GetDlgItem(s->hwnd, IDC_DLG_ICON);
                if (hIcon) MoveWindow(hIcon, iconX, iconY, iconSz, iconSz, TRUE);
            }

            HWND hText = GetDlgItem(s->hwnd, IDC_DLG_TEXT);
            if (hText) {
                SIZE sz = MeasureMultiline(s->spec.text, textW);
                int hh = (sz.cy > 0 ? sz.cy : 20);
                MoveWindow(hText, textX, textY, textW, hh, TRUE);
            }

            int n = (int)s->btnHwnds.size();
            std::vector<int> widths(n);
            int totalW = 0;
            for (int i = 0; i < n; ++i) {
                wchar_t txt[256] = { 0 };
                GetWindowTextW(s->btnHwnds[i], txt, 256);
                SIZE ts = MeasureSingleline(txt);
                int bw = ts.cx + btnPadW;
                if (bw < btnMinW) bw = btnMinW;
                widths[i] = bw;
                totalW += bw;
                if (i) totalW += btnGap;
            }

            int x = w - margin - totalW;
            int y = h - margin - btnH;
            for (int i = 0; i < n; ++i) {
                MoveWindow(s->btnHwnds[i], x, y, widths[i], btnH, TRUE);
                x += widths[i] + btnGap;
            }
        }

        void CloseWithResult(DialogState* s, int r) {
            s->result = r;
            s->done = true;
            DestroyWindow(s->hwnd);
        }

        void AcceptDefault(DialogState* s) {
            for (int i = 0; i < (int)s->btnHwnds.size(); ++i) {
                if (s->spec.buttons[i].isDefault) { CloseWithResult(s, i); return; }
            }
            if (!s->btnHwnds.empty()) CloseWithResult(s, 0);
        }

        LRESULT CALLBACK DialogProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
            DialogState* s = StateOf(h);

            switch (msg) {
            case WM_CREATE: {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
                s = reinterpret_cast<DialogState*>(cs->lpCreateParams);
                s->hwnd = h;
                SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)s);
                s->hInst = (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE);

                HINSTANCE hi = s->hInst;

                if (s->spec.iconResId) {
                    HICON ico = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(s->spec.iconResId),
                        IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR);
                    HWND hIcon = CreateWindowExW(0, L"STATIC", L"",
                        WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
                        0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_DLG_ICON, hi, NULL);
                    SendMessageW(hIcon, STM_SETICON, (WPARAM)ico, 0);
                }

                HWND hText = CreateWindowExW(0, L"STATIC", s->spec.text ? s->spec.text : L"",
                    WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                    0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_DLG_TEXT, hi, NULL);

                NONCLIENTMETRICSW ncm = { sizeof(ncm) };
                SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);

                if (ncm.lfMessageFont.lfHeight < 0) ncm.lfMessageFont.lfHeight -= 4;
                else ncm.lfMessageFont.lfHeight += 4;

                s->hFont = CreateFontIndirectW(&ncm.lfMessageFont);
                SendMessageW(hText, WM_SETFONT, (WPARAM)s->hFont, TRUE);

                s->btnHwnds.reserve(s->spec.buttonCount);
                for (int i = 0; i < s->spec.buttonCount; ++i) {
                    HWND btn = CreateButton(h, IDC_DLG_BTN + i,
                        s->spec.buttons[i].text, s->spec.buttons[i].style,
                        0, 0, 10, 10, hi);
                    if (s->spec.buttons[i].isDefault)
                        SetPropW(btn, L"uikit_default", (HANDLE)1);
                    s->btnHwnds.push_back(btn);
                }
                return 0;
            }

            case WM_SIZE:
                if (s) LayoutDialog(s, LOWORD(l), HIWORD(l));
                return 0;

            case WM_DRAWITEM: {
                auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(l);
                if (DrawButton(dis)) return TRUE;
                break;
            }

            case WM_COMMAND: {
                if (!s) break;
                int id = LOWORD(w);
                if (id >= IDC_DLG_BTN && id < IDC_DLG_BTN + (int)s->btnHwnds.size()) {
                    CloseWithResult(s, id - IDC_DLG_BTN);
                    return 0;
                }
                break;
            }

            case WM_KEYDOWN: {
                if (!s) break;
                if (w == VK_ESCAPE) {
                    if (s->btnHwnds.size() >= 2)
                        CloseWithResult(s, (int)s->btnHwnds.size() - 1);
                    else
                        CloseWithResult(s, -1);
                    return 0;
                }
                if (w == VK_RETURN) { AcceptDefault(s); return 0; }
                break;
            }

            case WM_CLOSE: {
                if (s) CloseWithResult(s, -1);
                return 0;
            }

            case WM_DESTROY:
                if (s && s->hFont) { DeleteObject(s->hFont); s->hFont = NULL; }
                return 0;
            }
            return DefWindowProcW(h, msg, w, l);
        }

        void RegisterDialogClass(HINSTANCE hInst) {
            static bool reg = false;
            if (reg) return;
            WNDCLASSEXW wc = { 0 };
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = &DialogProc;
            wc.hInstance = hInst;
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            wc.hbrBackground = CreateSolidBrush(Palette::kBg);
            wc.lpszClassName = kDialogClass;
            wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_ICON1));
            wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1),
                IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
            if (!RegisterClassExW(&wc)) {
                if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
            }
            reg = true;
        }

    }

    int ShowDialog(HWND owner, HINSTANCE hInst, const DialogSpec& specIn) {
        if (specIn.buttonCount <= 0 || !specIn.buttons) return -1;

        RegisterDialogClass(hInst);

        const int margin = 20;
        const int iconSz = 48;
        const int iconGap = 16;
        const int btnH = 34;
        const int btnMinW = 110;
        const int btnPadW = 40;
        const int btnGap = 10;
        const int textMinW = 260;
        const int textMaxW = 560;

        int textAvail = textMaxW;
        SIZE textSz = MeasureMultiline(specIn.text, textAvail);

        int desiredTextW = textSz.cx;
        if (desiredTextW < textMinW) desiredTextW = textMinW;
        if (desiredTextW > textMaxW) desiredTextW = textMaxW;

        textSz = MeasureMultiline(specIn.text, desiredTextW);
        int textH = textSz.cy;

        int n = specIn.buttonCount;
        std::vector<int> widths(n);
        int totalBtnsW = 0;
        for (int i = 0; i < n; ++i) {
            SIZE ts = MeasureSingleline(specIn.buttons[i].text);
            int bw = ts.cx + btnPadW;
            if (bw < btnMinW) bw = btnMinW;
            widths[i] = bw;
            totalBtnsW += bw;
            if (i) totalBtnsW += btnGap;
        }

        int iconTotalW = specIn.iconResId ? (iconSz + iconGap) : 0;

        int clientW = margin * 2 + iconTotalW + desiredTextW;
        int btnsNeedW = margin * 2 + totalBtnsW;
        if (clientW < btnsNeedW) clientW = btnsNeedW;

        int contentTop = margin;
        int contentH = textH;
        if (contentH < iconSz) contentH = iconSz;

        int clientH = contentTop + contentH + 50 + btnH + margin;

        RECT rc = { 0, 0, clientW, clientH };
        DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP;
        DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
        AdjustWindowRectEx(&rc, style, FALSE, exStyle);

        int W = rc.right - rc.left;
        int H = rc.bottom - rc.top;

        RECT ownerRect = { 0, 0, 0, 0 };
        if (owner && IsWindow(owner)) GetWindowRect(owner, &ownerRect);
        else {
            ownerRect.right = GetSystemMetrics(SM_CXSCREEN);
            ownerRect.bottom = GetSystemMetrics(SM_CYSCREEN);
        }
        int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - W) / 2;
        int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - H) / 2;

        DialogState s;
        s.spec = specIn;

        HWND hWnd = CreateWindowExW(
            exStyle,
            kDialogClass, specIn.title ? specIn.title : L"Overlay",
            style,
            x, y, W, H, owner, NULL, hInst, &s);
        if (!hWnd) return -1;

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
                DispatchMessage(&msg);
            }
        }

        if (owner) EnableWindow(owner, TRUE);
        return s.result;
    }

    static int ShowOne(HWND owner, HINSTANCE hInst,
        const wchar_t* title, const wchar_t* text)
    {
        DialogButton b = { L"OK", ButtonStyle::Normal, true };
        DialogSpec sp;
        sp.title = title;
        sp.text = text;
        sp.buttons = &b;
        sp.buttonCount = 1;
        return ShowDialog(owner, hInst, sp);
    }

    int ShowInfo(HWND owner, HINSTANCE hInst, const wchar_t* t, const wchar_t* x) {
        return ShowOne(owner, hInst, t, x);
    }
    int ShowWarning(HWND owner, HINSTANCE hInst, const wchar_t* t, const wchar_t* x) {
        return ShowOne(owner, hInst, t, x);
    }
    int ShowError(HWND owner, HINSTANCE hInst, const wchar_t* t, const wchar_t* x) {
        return ShowOne(owner, hInst, t, x);
    }

    int ShowYesNo(HWND owner, HINSTANCE hInst, const wchar_t* t, const wchar_t* x) {
        DialogButton bs[2] = {
            { L"Yes",    ButtonStyle::Normal, true },
            { L"Cancel", ButtonStyle::Normal, false },
        };
        DialogSpec sp;
        sp.title = t;
        sp.text = x;
        sp.buttons = bs;
        sp.buttonCount = 2;
        return ShowDialog(owner, hInst, sp) == 0 ? 1 : 0;
    }

}