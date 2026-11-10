// ============================================================================
//  overlay.h  — общие объявления, утилиты, структуры данных
// ============================================================================
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/ENTRY:wWinMainCRTStartup")

#include <windows.h>
#include <windowsx.h>
#include <wincodec.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>
#include <cwchar>

#include "resource.h"

#ifndef IDI_ICON1
#define IDI_ICON1 101
#endif
#ifndef IMAGE1
#define IMAGE1 200
#endif
#ifndef IMAGE2
#define IMAGE2 201
#endif

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Ole32.lib")

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
//  Константы (inline constexpr — чтобы можно было держать в заголовке)
// ---------------------------------------------------------------------------
inline constexpr int MAX_IMAGES = 4;

struct ImageSlotDef {
    int            resId;
    const wchar_t* defaultFileName;
    const wchar_t* iniKey;
};

extern const ImageSlotDef kSlots[MAX_IMAGES];

struct DefaultLayout { int x, y, w; };
extern const DefaultLayout DEFAULT_LAYOUT[MAX_IMAGES];

extern ComPtr<IWICImagingFactory> g_wicFactory;
extern ComPtr<ID2D1Factory>       g_d2dFactory;

extern RECT  g_monRect;
extern UINT  g_currentDpi;

inline constexpr int   MIN_SIZE = 20;
inline constexpr int   MAX_SIZE = 4000;
inline constexpr float HIT_ZONE_DIP = 20.0f;

inline constexpr float ROTATE_SNAP_STEP_DEG = 15.0f;
inline constexpr float ROTATE_SNAP_THRESHOLD_DEG = 3.0f;

inline constexpr int CLOSE_BTN_SIZE = 40;
inline constexpr int CLOSE_BTN_MARGIN = 20;
inline constexpr int VIS_BTN_SIZE = 28;
inline constexpr int VIS_BTN_MARGIN = 8;

inline constexpr int TIMER_KEEP_TOPMOST = 100;
inline constexpr int TIMER_WATCH_GAME = 101;
inline constexpr int GAME_POLL_INTERVAL_MS = 250;

inline constexpr UINT WM_APP_SHOW_OVERLAY = WM_APP + 1;

// ---------------------------------------------------------------------------
//  ID горячих клавиш (единый источник для регистрации и для таблицы полей)
// ---------------------------------------------------------------------------
inline constexpr int HOTKEY_TOGGLE_VISIBLE = 1;
inline constexpr int HOTKEY_TOGGLE_EDIT = 2;
inline constexpr int HOTKEY_RESET_POS = 3;
inline constexpr int HOTKEY_RELOAD_IMAGES = 4;
inline constexpr int HOTKEY_CAPTURE_FG = 5;
inline constexpr int HOTKEY_REMOVE_FG = 6;
inline constexpr int HOTKEY_EDIT_HOTKEYS = 7;

extern const wchar_t* CONFIG_FILE;
extern const wchar_t* TARGET_ANY_MARKER;
extern const wchar_t* TARGET_NONE_MARKER;

// ---------------------------------------------------------------------------
//  Общие структуры
// ---------------------------------------------------------------------------
struct TargetPolicy {
    std::vector<std::wstring> processes;
    bool showEverywhere = false;
};

// ---------------------------------------------------------------------------
//  Утилиты (общие)
// ---------------------------------------------------------------------------
inline constexpr float PI_F = 3.14159265358979323846f;
inline float DegToRad(float d) { return d * PI_F / 180.0f; }
inline float DipToPx(float dip) { return dip * (float)g_currentDpi / 96.0f; }
inline int   DipToPxI(int dip) { return (int)std::lround((double)dip * g_currentDpi / 96.0); }
inline int   PxToDipI(int px) { return (int)std::lround((double)px * 96.0 / g_currentDpi); }

bool BuildExeRelativePath(const wchar_t* fileName, wchar_t* outPath, DWORD cch);
bool FileExists(const wchar_t* path);
int  EffectiveMaxSize();

// WIC
HRESULT DecodeToBGRA(IWICBitmapDecoder* decoder,
    IWICBitmapSource** outBitmap, int& outW, int& outH);
HRESULT LoadPNGFromResourceWIC(HINSTANCE hInst, int resId,
    IWICBitmapSource** outBitmap, int& outW, int& outH);
HRESULT LoadPNGFromFileWIC(const wchar_t* filePath,
    IWICBitmapSource** outBitmap, int& outW, int& outH);

// ---------------------------------------------------------------------------
//  OverlayImage
// ---------------------------------------------------------------------------
enum ResizeMode { RESIZE_NONE = 0, RESIZE_CORNER, RESIZE_WIDTH_ONLY, RESIZE_HEIGHT_ONLY };

struct OverlayImage {
    ComPtr<IWICBitmapSource> pImage;
    int x = 0, y = 0, width = 0, height = 0;
    int nativeW = 0, nativeH = 0;
    bool isDragging = false;
    bool isResizing = false;
    bool isRotating = false;
    bool visible = true;
    ResizeMode resizeMode = RESIZE_NONE;
    int dragOffsetX = 0, dragOffsetY = 0;

    float rotation = 0.0f;
    float rotateStartMouseAngle = 0.0f;
    float rotateStartRotation = 0.0f;

    OverlayImage() = default;
    OverlayImage(const OverlayImage&) = delete;
    OverlayImage& operator=(const OverlayImage&) = delete;
    OverlayImage(OverlayImage&&) = default;
    OverlayImage& operator=(OverlayImage&&) = default;
};

extern std::vector<OverlayImage> g_images;

// ---------------------------------------------------------------------------
//  Геометрия (свободные функции, оперируют OverlayImage)
// ---------------------------------------------------------------------------
void  GetImageCenterDip(const OverlayImage& img, float& cx, float& cy);
void  DipToLocal(const OverlayImage& img, float dipX, float dipY, float& lx, float& ly);
void  LocalToDip(const OverlayImage& img, float lx, float ly, float& dipX, float& dipY);
void  GetVisButtonCenterDip(const OverlayImage& img, float& cx, float& cy);
D2D1_RECT_F GetVisButtonRectDip(const OverlayImage& img);
bool  IsPointInVisButton(OverlayImage& img, int pxX, int pxY);

void  SetImageWidthKeepAspect(OverlayImage& img, int newW);
void  SetImageHeightKeepAspect(OverlayImage& img, int newH);
void  KeepLocalPointAtScreen(OverlayImage& img, float fixedLx, float fixedLy,
    float screenX, float screenY);
void  ResizeCorner(OverlayImage& img, int pxX, int pxY);
void  ResizeWidthOnly(OverlayImage& img, int pxX, int pxY);
void  ResizeHeightOnly(OverlayImage& img, int pxX, int pxY);

bool  IsShiftDown();
float SnapAngleNear(float deg, float target, float threshold);
void  StartRotate(OverlayImage& img, int pxX, int pxY);
void  UpdateRotate(OverlayImage& img, int pxX, int pxY);

struct HitMask {
    bool inImage = false;
    bool inRotate = false;
    bool inResizeCorner = false;
    bool inRightEdge = false;
    bool inBottomEdge = false;
};
HitMask ComputeHitMask(OverlayImage& img, int pxX, int pxY);
bool    IsPointInImage(OverlayImage& img, int pxX, int pxY);
void    ZoomImage(OverlayImage& img, int delta);

void  GetCloseButtonCenter(int& cx, int& cy);
bool  IsPointInCloseButton(int pxX, int pxY, bool editMode);