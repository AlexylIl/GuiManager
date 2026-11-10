// ============================================================================
//  ui_kit.h  — общий UI-kit: палитра, owner-draw кнопки, модальные диалоги
// ============================================================================
#pragma once
#include "overlay.h"

namespace UiKit {

    // ---------------------------------------------------------------------------
    //  Палитра (единая для всех окон)
    // ---------------------------------------------------------------------------
    namespace Palette {
        inline constexpr COLORREF kBg = RGB(250, 250, 250);
        inline constexpr COLORREF kText = RGB(0, 0, 0);
        inline constexpr COLORREF kTextDim = RGB(110, 110, 110);

        inline constexpr COLORREF kBtnBgNormal = RGB(250, 250, 250);
        inline constexpr COLORREF kBtnBgHover = RGB(229, 241, 251);
        inline constexpr COLORREF kBtnBgPressed = RGB(204, 228, 247);
        inline constexpr COLORREF kBtnBorderNormal = RGB(173, 173, 173);
        inline constexpr COLORREF kBtnBorderHover = RGB(0, 120, 215);
        inline constexpr COLORREF kBtnBorderPressed = RGB(0, 90, 158);
        inline constexpr COLORREF kBtnTextNormal = RGB(0, 0, 0);
        inline constexpr COLORREF kBtnTextDisabled = RGB(160, 160, 160);

        inline constexpr COLORREF kAccentBg = RGB(255, 244, 206);
        inline constexpr COLORREF kAccentBorder = RGB(255, 185, 0);
        inline constexpr COLORREF kAccentText = RGB(122, 79, 0);

        inline constexpr COLORREF kDangerNormal = RGB(200, 45, 45);
        inline constexpr COLORREF kDangerHover = RGB(230, 57, 57);
        inline constexpr COLORREF kDangerPressed = RGB(150, 30, 30);
        inline constexpr COLORREF kDangerText = RGB(255, 255, 255);
    }

    // ---------------------------------------------------------------------------
    //  Owner-draw кнопка.
    // ---------------------------------------------------------------------------
    enum class ButtonStyle { Normal, Danger };

    HWND CreateButton(HWND parent, int id, const wchar_t* text,
        ButtonStyle style, int x, int y, int w, int h,
        HINSTANCE hInst);

    bool DrawButton(const DRAWITEMSTRUCT* dis);

    // ---------------------------------------------------------------------------
    //  Модальный диалог
    // ---------------------------------------------------------------------------
    struct DialogButton {
        const wchar_t* text;
        ButtonStyle    style = ButtonStyle::Normal;
        bool           isDefault = false;
    };

    struct DialogSpec {
        const wchar_t* title = L"Overlay";
        const wchar_t* text = L"";
        UINT           iconResId = 0;
        const DialogButton* buttons = nullptr;
        int            buttonCount = 0;
    };

    int ShowDialog(HWND owner, HINSTANCE hInst, const DialogSpec& spec);

    int ShowInfo(HWND owner, HINSTANCE hInst, const wchar_t* title, const wchar_t* text);
    int ShowWarning(HWND owner, HINSTANCE hInst, const wchar_t* title, const wchar_t* text);
    int ShowError(HWND owner, HINSTANCE hInst, const wchar_t* title, const wchar_t* text);
    int ShowYesNo(HWND owner, HINSTANCE hInst, const wchar_t* title, const wchar_t* text);

} // namespace UiKit