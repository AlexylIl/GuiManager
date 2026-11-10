// ============================================================================
//  layout_store.cpp
// ============================================================================
#include "layout_store.h"

LayoutStore::LayoutStore(ConfigStore& cfg, const std::wstring& owner)
    : m_cfg(cfg), m_owner(owner) {}

void LayoutStore::SetOwner(const std::wstring& proc) {
    m_owner = proc;
}

void LayoutStore::MarkDirty() {
    m_dirty = true;
    m_dirtyOwner = m_owner;
}

void LayoutStore::LoadForCurrent(std::vector<OverlayImage>& images) {
    if (m_dirty && !m_dirtyOwner.empty() && m_dirtyOwner != m_owner)
        m_cfg.SaveLayoutForProcess(m_dirtyOwner, images, false);

    m_dirty = false;
    m_dirtyOwner.clear();

    m_cfg.LoadLayoutForProcess(m_owner, images);
}

bool LayoutStore::SaveDirty(const std::vector<OverlayImage>& images) {
    if (m_dirtyOwner.empty() || !m_dirty) return true;
    bool ok = m_cfg.SaveLayoutForProcess(m_dirtyOwner, images, false);
    if (ok) {
        m_dirty = false;
        m_dirtyOwner.clear();
    }
    return ok;
}

bool LayoutStore::SaveAll(const TargetPolicy& targets,
    const std::vector<OverlayImage>& images)
{
    bool a = m_cfg.SaveGlobalConfig(targets);
    bool b = SaveDirty(images);
    return a && b;
}