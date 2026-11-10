// ============================================================================
//  geometry.cpp
// ============================================================================
#include "overlay.h"

void GetImageCenterDip(const OverlayImage& img, float& cx, float& cy) {
    cx = (float)img.x + img.width / 2.0f;
    cy = (float)img.y + img.height / 2.0f;
}

void DipToLocal(const OverlayImage& img, float dipX, float dipY, float& lx, float& ly) {
    float cx, cy; GetImageCenterDip(img, cx, cy);
    float rad = DegToRad(img.rotation);
    float c = cosf(rad), s = sinf(rad);

    float dx = dipX - cx;
    float dy = dipY - cy;

    float rx = dx * c + dy * s;
    float ry = -dx * s + dy * c;

    lx = rx + img.width / 2.0f;
    ly = ry + img.height / 2.0f;
}

void LocalToDip(const OverlayImage& img, float lx, float ly, float& dipX, float& dipY) {
    float cx, cy; GetImageCenterDip(img, cx, cy);
    float rad = DegToRad(img.rotation);
    float c = cosf(rad), s = sinf(rad);

    float dx = lx - img.width / 2.0f;
    float dy = ly - img.height / 2.0f;

    dipX = cx + dx * c - dy * s;
    dipY = cy + dx * s + dy * c;
}

void GetVisButtonCenterDip(const OverlayImage& img, float& cx, float& cy) {
    float trX, trY;
    LocalToDip(img, (float)img.width, 0.0f, trX, trY);

    float rad = DegToRad(img.rotation);
    float c = cosf(rad), s = sinf(rad);
    const float lvx = 0.70710678f;
    const float lvy = -0.70710678f;
    float evx = lvx * c - lvy * s;
    float evy = lvx * s + lvy * c;

    float half = (float)VIS_BTN_SIZE / 2.0f;
    float m = (float)VIS_BTN_MARGIN;

    cx = trX + evx * (m + half);
    cy = trY + evy * (m + half);

    int screenWdip = PxToDipI(g_monRect.right - g_monRect.left);
    int screenHdip = PxToDipI(g_monRect.bottom - g_monRect.top);
    float lo = half + 2.0f;
    float hiX = (float)screenWdip - half - 2.0f;
    float hiY = (float)screenHdip - half - 2.0f;
    if (hiX >= lo) cx = std::clamp(cx, lo, hiX);
    if (hiY >= lo) cy = std::clamp(cy, lo, hiY);
}

D2D1_RECT_F GetVisButtonRectDip(const OverlayImage& img) {
    float cx, cy;
    GetVisButtonCenterDip(img, cx, cy);
    float half = (float)VIS_BTN_SIZE / 2.0f;
    return D2D1::RectF(cx - half, cy - half, cx + half, cy + half);
}

bool IsPointInVisButton(OverlayImage& img, int pxX, int pxY) {
    if (!img.pImage) return false;
    D2D1_RECT_F r = GetVisButtonRectDip(img);
    float mx = (float)PxToDipI(pxX);
    float my = (float)PxToDipI(pxY);
    const float pad = 4.0f;
    return (mx >= r.left - pad && mx <= r.right + pad &&
        my >= r.top - pad && my <= r.bottom + pad);
}

void SetImageWidthKeepAspect(OverlayImage& img, int newW) {
    int cap = EffectiveMaxSize();
    newW = std::clamp(newW, MIN_SIZE, cap);
    if (img.nativeW <= 0 || img.nativeH <= 0) { img.width = newW; return; }
    img.width = newW;
    img.height = std::clamp(
        (int)std::lround((double)newW * img.nativeH / img.nativeW),
        MIN_SIZE, cap);
}

void SetImageHeightKeepAspect(OverlayImage& img, int newH) {
    int cap = EffectiveMaxSize();
    newH = std::clamp(newH, MIN_SIZE, cap);
    if (img.nativeW <= 0 || img.nativeH <= 0) { img.height = newH; return; }
    img.height = newH;
    img.width = std::clamp(
        (int)std::lround((double)newH * img.nativeW / img.nativeH),
        MIN_SIZE, cap);
}

void KeepLocalPointAtScreen(OverlayImage& img,
    float fixedLx, float fixedLy, float screenX, float screenY)
{
    float cx, cy; GetImageCenterDip(img, cx, cy);
    float rad = DegToRad(img.rotation);
    float c = cosf(rad), s = sinf(rad);

    float dx = fixedLx - img.width / 2.0f;
    float dy = fixedLy - img.height / 2.0f;
    float ex = dx * c - dy * s;
    float ey = dx * s + dy * c;

    float targetCx = screenX - ex;
    float targetCy = screenY - ey;

    img.x = (int)std::lround(targetCx - img.width / 2.0f);
    img.y = (int)std::lround(targetCy - img.height / 2.0f);
}

void ResizeCorner(OverlayImage& img, int pxX, int pxY) {
    float fixedX, fixedY;
    LocalToDip(img, 0.0f, 0.0f, fixedX, fixedY);
    float lx, ly;
    DipToLocal(img, (float)PxToDipI(pxX), (float)PxToDipI(pxY), lx, ly);
    float ratioW = (img.width > 0) ? (lx / (float)img.width) : 1.0f;
    float ratioH = (img.height > 0) ? (ly / (float)img.height) : 1.0f;
    float ratio = std::max(ratioW, ratioH);
    if (ratio <= 0.0f) ratio = 0.01f;
    SetImageWidthKeepAspect(img, (int)std::lround(img.width * ratio));
    KeepLocalPointAtScreen(img, 0.0f, 0.0f, fixedX, fixedY);
}

void ResizeWidthOnly(OverlayImage& img, int pxX, int pxY) {
    float fixedX, fixedY;
    LocalToDip(img, 0.0f, (float)img.height / 2.0f, fixedX, fixedY);
    float lx, ly;
    DipToLocal(img, (float)PxToDipI(pxX), (float)PxToDipI(pxY), lx, ly);
    img.width = std::clamp((int)std::lround(lx), MIN_SIZE, EffectiveMaxSize());
    KeepLocalPointAtScreen(img, 0.0f, (float)img.height / 2.0f, fixedX, fixedY);
}

void ResizeHeightOnly(OverlayImage& img, int pxX, int pxY) {
    float fixedX, fixedY;
    LocalToDip(img, (float)img.width / 2.0f, 0.0f, fixedX, fixedY);
    float lx, ly;
    DipToLocal(img, (float)PxToDipI(pxX), (float)PxToDipI(pxY), lx, ly);
    img.height = std::clamp((int)std::lround(ly), MIN_SIZE, EffectiveMaxSize());
    KeepLocalPointAtScreen(img, (float)img.width / 2.0f, 0.0f, fixedX, fixedY);
}

bool IsShiftDown() { return (GetKeyState(VK_SHIFT) & 0x8000) != 0; }

float SnapAngleNear(float deg, float target, float threshold) {
    if (std::fabs(deg - target) <= threshold) return target;
    return deg;
}

void StartRotate(OverlayImage& img, int pxX, int pxY) {
    float cx, cy; GetImageCenterDip(img, cx, cy);
    float mx = (float)PxToDipI(pxX);
    float my = (float)PxToDipI(pxY);
    img.rotateStartMouseAngle = atan2f(my - cy, mx - cx);
    img.rotateStartRotation = img.rotation;
}

void UpdateRotate(OverlayImage& img, int pxX, int pxY) {
    float cx, cy; GetImageCenterDip(img, cx, cy);
    float mx = (float)PxToDipI(pxX);
    float my = (float)PxToDipI(pxY);
    float cur = atan2f(my - cy, mx - cx);
    float deltaDeg = (cur - img.rotateStartMouseAngle) * 180.0f / PI_F;
    float rot = img.rotateStartRotation + deltaDeg;

    if (IsShiftDown()) {
        rot = std::round(rot / ROTATE_SNAP_STEP_DEG) * ROTATE_SNAP_STEP_DEG;
    }
    else {
        rot = SnapAngleNear(rot, 0.0f, ROTATE_SNAP_THRESHOLD_DEG);
        rot = SnapAngleNear(rot, 90.0f, ROTATE_SNAP_THRESHOLD_DEG);
        rot = SnapAngleNear(rot, 180.0f, ROTATE_SNAP_THRESHOLD_DEG);
        rot = SnapAngleNear(rot, 270.0f, ROTATE_SNAP_THRESHOLD_DEG);
        rot = SnapAngleNear(rot, 360.0f, ROTATE_SNAP_THRESHOLD_DEG);
    }
    while (rot < 0.0f)   rot += 360.0f;
    while (rot >= 360.0f) rot -= 360.0f;
    img.rotation = rot;
}

static float LocalHitSize(const OverlayImage& img) {
    float m = (float)std::min(img.width, img.height);
    return std::min(HIT_ZONE_DIP, m * 0.25f);
}

HitMask ComputeHitMask(OverlayImage& img, int pxX, int pxY) {
    HitMask m;
    float lx, ly;
    DipToLocal(img, (float)PxToDipI(pxX), (float)PxToDipI(pxY), lx, ly);
    float cs = LocalHitSize(img);
    float W = (float)img.width;
    float H = (float)img.height;
    m.inImage = (lx >= 0.0f && lx <= W && ly >= 0.0f && ly <= H);
    m.inRotate = (lx >= 0.0f && lx <= cs && ly >= 0.0f && ly <= cs);
    m.inResizeCorner = (lx >= W - cs && lx <= W && ly >= H - cs && ly <= H);
    m.inRightEdge = (lx >= W - cs && lx <= W && ly > cs && ly < H - cs);
    m.inBottomEdge = (ly >= H - cs && ly <= H && lx > cs && lx < W - cs);
    return m;
}

bool IsPointInImage(OverlayImage& img, int pxX, int pxY) {
    return ComputeHitMask(img, pxX, pxY).inImage;
}

void ZoomImage(OverlayImage& img, int delta) {
    if (!img.pImage) return;
    float factor = (delta > 0) ? 1.1f : 0.9f;
    int newW = (int)(img.width * factor + 0.5f);
    float cx, cy; GetImageCenterDip(img, cx, cy);
    SetImageWidthKeepAspect(img, newW);
    img.x = (int)std::lround(cx - img.width / 2.0f);
    img.y = (int)std::lround(cy - img.height / 2.0f);
}

void GetCloseButtonCenter(int& cx, int& cy) {
    int monWpx = g_monRect.right - g_monRect.left;
    int screenWdip = PxToDipI(monWpx);
    cx = screenWdip - CLOSE_BTN_MARGIN - CLOSE_BTN_SIZE / 2;
    cy = CLOSE_BTN_MARGIN + CLOSE_BTN_SIZE / 2;
}

bool IsPointInCloseButton(int pxX, int pxY, bool editMode) {
    if (!editMode) return false;
    int cx, cy; GetCloseButtonCenter(cx, cy);
    int dx = PxToDipI(pxX) - cx;
    int dy = PxToDipI(pxY) - cy;
    int r = CLOSE_BTN_SIZE / 2;
    return (dx * dx + dy * dy) <= (r * r);
}