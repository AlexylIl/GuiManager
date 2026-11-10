// ============================================================================
//  layout_store.h  — состояние «layout изменён» + сохранение/загрузка
// ============================================================================
#pragma once
#include "overlay.h"
#include "config_store.h"

class LayoutStore {
public:
    LayoutStore(ConfigStore& cfg, const std::wstring& owner);

    void SetOwner(const std::wstring& proc);

    void MarkDirty();
    bool IsDirty() const { return m_dirty; }
    const std::wstring& DirtyOwner() const { return m_dirtyOwner; }

    void LoadForCurrent(std::vector<OverlayImage>& images);

    bool SaveDirty(const std::vector<OverlayImage>& images);

    bool SaveAll(const TargetPolicy& targets,
        const std::vector<OverlayImage>& images);

private:
    ConfigStore& m_cfg;
    std::wstring m_owner;
    std::wstring m_dirtyOwner;
    bool         m_dirty = false;
};