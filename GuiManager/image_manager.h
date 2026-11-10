// ============================================================================
//  image_manager.h
// ============================================================================
#pragma once
#include "overlay.h"

class ImageManager {
public:
    ImageManager() = default;
    ~ImageManager() = default;

    // Загрузка одного слота:
    //   1) per-process картинка (в папке <exe>\<procname>\)
    //   2) общая картинка рядом с exe
    //   3) встроенный ресурс (если resId != 0)
    // Возвращает S_OK / E_NOTIMPL (ресурса нет) / ошибку WIC.
    HRESULT LoadSlot(HINSTANCE hInst, int slotIndex,
        const std::wstring& targetProcess,
        OverlayImage& img, bool& usedCustom,
        std::wstring* diagnostics) const;

    // Перезагрузка всех слотов с сохранением размеров/поворота/видимости
    void ReloadAll(HINSTANCE hInst, const std::wstring& targetProcess,
        std::vector<OverlayImage>& images,
        std::wstring* diagOut) const;
};