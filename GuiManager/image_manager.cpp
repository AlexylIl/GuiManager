// ============================================================================
//  image_manager.cpp
// ============================================================================
#include "overlay.h"
#include "config_store.h"
#include "image_manager.h"

// ---------- общие утилиты ----------
bool BuildExeRelativePath(const wchar_t* fileName, wchar_t* outPath, DWORD cch) {
    if (!outPath || cch == 0 || !fileName) return false;
    outPath[0] = 0;

    wchar_t exePath[MAX_PATH] = { 0 };
    DWORD n = GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;

    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *(lastSlash + 1) = 0;
    else exePath[0] = 0;

    size_t need = wcslen(exePath) + wcslen(fileName) + 1;
    if (need > (size_t)cch) return false;

    wcscpy_s(outPath, cch, exePath);
    wcscat_s(outPath, cch, fileName);
    return true;
}

bool FileExists(const wchar_t* path) {
    if (!path || !path[0]) return false;
    DWORD attr = GetFileAttributesW(path);
    return (attr != INVALID_FILE_ATTRIBUTES) &&
        !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

int EffectiveMaxSize() {
    int screenW = g_monRect.right - g_monRect.left;
    int screenH = g_monRect.bottom - g_monRect.top;
    int maxDim = std::max(screenW, screenH);
    if (maxDim <= 0) return MAX_SIZE;
    return std::min(MAX_SIZE, maxDim);
}

// ---------- WIC ----------
HRESULT DecodeToBGRA(IWICBitmapDecoder* decoder,
    IWICBitmapSource** outBitmap, int& outW, int& outH)
{
    *outBitmap = nullptr; outW = outH = 0;
    if (!decoder) return E_POINTER;

    ComPtr<IWICBitmapFrameDecode> frame;
    HRESULT hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) return hr;

    ComPtr<IWICFormatConverter> converter;
    hr = g_wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr)) return hr;

    hr = converter->Initialize(frame.Get(),
        GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
        nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return hr;

    UINT w = 0, h = 0;
    hr = converter->GetSize(&w, &h);
    if (FAILED(hr)) return hr;

    outW = (int)w; outH = (int)h;
    *outBitmap = converter.Detach();
    return S_OK;
}

HRESULT LoadPNGFromResourceWIC(HINSTANCE hInst, int resId,
    IWICBitmapSource** outBitmap, int& outW, int& outH)
{
    *outBitmap = nullptr; outW = outH = 0;
    if (resId == 0) return E_NOTIMPL;

    HRSRC hRes = FindResourceW(hInst, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!hRes) return HRESULT_FROM_WIN32(GetLastError());

    DWORD size = SizeofResource(hInst, hRes);
    if (size == 0) return E_FAIL;

    HGLOBAL hGlob = LoadResource(hInst, hRes);
    if (!hGlob) return HRESULT_FROM_WIN32(GetLastError());

    void* pData = LockResource(hGlob);
    if (!pData) return E_FAIL;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) return E_OUTOFMEMORY;
    void* pCopy = GlobalLock(hMem);
    if (!pCopy) { GlobalFree(hMem); return E_OUTOFMEMORY; }
    memcpy(pCopy, pData, size);
    GlobalUnlock(hMem);

    ComPtr<IStream> stream;
    HRESULT hr = CreateStreamOnHGlobal(hMem, TRUE, &stream);
    if (FAILED(hr)) { GlobalFree(hMem); return hr; }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = g_wicFactory->CreateDecoderFromStream(stream.Get(), nullptr,
        WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) return hr;

    return DecodeToBGRA(decoder.Get(), outBitmap, outW, outH);
}

HRESULT LoadPNGFromFileWIC(const wchar_t* filePath,
    IWICBitmapSource** outBitmap, int& outW, int& outH)
{
    *outBitmap = nullptr; outW = outH = 0;

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = g_wicFactory->CreateDecoderFromFilename(filePath, nullptr,
        GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) return hr;

    return DecodeToBGRA(decoder.Get(), outBitmap, outW, outH);
}

// ---------- ImageManager ----------
HRESULT ImageManager::LoadSlot(HINSTANCE hInst, int slotIndex,
    const std::wstring& targetProcess,
    OverlayImage& img, bool& usedCustom,
    std::wstring* diagnostics) const
{
    if (slotIndex < 0 || slotIndex >= MAX_IMAGES) return E_INVALIDARG;

    const ImageSlotDef& slot = kSlots[slotIndex];
    usedCustom = false;
    img.pImage.Reset();
    img.nativeW = img.nativeH = 0;

    // 1. per-process картинка
    if (!targetProcess.empty()) {
        wchar_t procPath[MAX_PATH] = { 0 };
        ConfigStore cfg;
        if (cfg.GetProcessImagePath(targetProcess, slot.defaultFileName,
            procPath, MAX_PATH) &&
            FileExists(procPath))
        {
            HRESULT hr = LoadPNGFromFileWIC(procPath,
                img.pImage.ReleaseAndGetAddressOf(),
                img.nativeW, img.nativeH);
            if (SUCCEEDED(hr)) { usedCustom = true; return S_OK; }
            if (diagnostics) {
                wchar_t line[512];
                swprintf_s(line,
                    L"slot res=%d: failed to load '%s' (HRESULT=0x%08X)\n",
                    slot.resId, procPath, hr);
                *diagnostics += line;
            }
            img.pImage.Reset();
            img.nativeW = img.nativeH = 0;
        }
    }

    // 2. общая картинка рядом с exe
    wchar_t customPath[MAX_PATH] = { 0 };
    bool pathOk = BuildExeRelativePath(slot.defaultFileName, customPath, MAX_PATH);

    if (!pathOk) {
        if (diagnostics) {
            wchar_t line[256];
            swprintf_s(line,
                L"slot res=%d: path too long or module path unavailable "
                L"(file '%s')\n", slot.resId,
                slot.defaultFileName ? slot.defaultFileName : L"?");
            *diagnostics += line;
        }
    }
    else if (FileExists(customPath)) {
        HRESULT hr = LoadPNGFromFileWIC(customPath,
            img.pImage.ReleaseAndGetAddressOf(),
            img.nativeW, img.nativeH);
        if (SUCCEEDED(hr)) { usedCustom = true; return S_OK; }
        if (diagnostics) {
            wchar_t line[512];
            swprintf_s(line,
                L"slot res=%d: failed to load '%s' (HRESULT=0x%08X)\n",
                slot.resId, customPath, hr);
            *diagnostics += line;
        }
        img.pImage.Reset();
        img.nativeW = img.nativeH = 0;
    }

    // 3. ресурс
    if (slot.resId == 0) return E_NOTIMPL;

    HRESULT hr = LoadPNGFromResourceWIC(hInst, slot.resId,
        img.pImage.ReleaseAndGetAddressOf(),
        img.nativeW, img.nativeH);
    if (FAILED(hr) && diagnostics) {
        wchar_t line[256];
        swprintf_s(line,
            L"slot res=%d: embedded resource load failed (HRESULT=0x%08X)\n",
            slot.resId, hr);
        *diagnostics += line;
    }
    return hr;
}

void ImageManager::ReloadAll(HINSTANCE hInst, const std::wstring& targetProcess,
    std::vector<OverlayImage>& images,
    std::wstring* diagOut) const
{
    for (int i = 0; i < (int)images.size() && i < MAX_IMAGES; ++i) {
        OverlayImage& img = images[i];
        int   prevW = img.width;
        int   prevH = img.height;
        float rot = img.rotation;
        bool  vis = img.visible;
        bool  usedCustom = false;

        HRESULT hr = LoadSlot(hInst, i, targetProcess, img, usedCustom, diagOut);
        bool hasNow = (img.pImage != nullptr);

        if (SUCCEEDED(hr) && hasNow) {
            int targetW = (prevW > 0) ? prevW : DEFAULT_LAYOUT[i].w;
            SetImageWidthKeepAspect(img, targetW);
        }
        else {
            img.width = prevW;
            img.height = prevH;
        }
        img.rotation = rot;
        img.visible = vis;
    }
}