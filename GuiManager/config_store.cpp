// ============================================================================
//  config_store.cpp
// ============================================================================
#include "overlay.h"
#include "config_store.h"

#include <iterator>   // std::size

const ImageSlotDef kSlots[MAX_IMAGES] = {
    { IMAGE1, L"overlay_img1.png", L"img1" },
    { IMAGE2, L"overlay_img2.png", L"img2" },
    { 0,      L"overlay_img3.png", L"img3" },
    { 0,      L"overlay_img4.png", L"img4" },
};

const DefaultLayout DEFAULT_LAYOUT[MAX_IMAGES] = {
    { 100, 100, 300 },
    { 500, 100, 300 },
    { 100, 500, 300 },
    { 500, 500, 300 },
};

const wchar_t* CONFIG_FILE = L"overlay_config.ini";
const wchar_t* TARGET_ANY_MARKER = L"*";
const wchar_t* TARGET_NONE_MARKER = L"-";


// ---------- путь / шаблон ----------
bool ConfigStore::GetConfigPath(wchar_t* outPath, DWORD cch) const {
    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *(lastSlash + 1) = 0;
    return swprintf_s(outPath, cch, L"%s%s", exePath, CONFIG_FILE) >= 0;
}

bool ConfigStore::GetProcessDir(const std::wstring& procName,
    wchar_t* outDir, DWORD cch, bool create) const {
    if (!outDir || cch == 0) return false;
    outDir[0] = 0;
    if (procName.empty()) return false;

    wchar_t exePath[MAX_PATH] = { 0 };
    DWORD n = GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;

    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *(lastSlash + 1) = 0;
    else exePath[0] = 0;

    size_t need = wcslen(exePath) + procName.size() + 2;
    if (need > (size_t)cch) return false;

    wcscpy_s(outDir, cch, exePath);
    wcscat_s(outDir, cch, procName.c_str());
    wcscat_s(outDir, cch, L"\\");

    if (create) {
        if (!CreateDirectoryW(outDir, NULL)) {
            DWORD err = GetLastError();
            if (err != ERROR_ALREADY_EXISTS) {
                OutputDebugStringW(L"[ConfigStore] GetProcessDir: CreateDirectory failed\n");
                outDir[0] = 0;
                return false;
            }
        }
    }
    return true;
}

bool ConfigStore::GetProcessConfigPath(const std::wstring& procName,
    wchar_t* outPath, DWORD cch, bool create) const {
    wchar_t dir[MAX_PATH] = { 0 };
    if (!GetProcessDir(procName, dir, MAX_PATH, create)) return false;
    return swprintf_s(outPath, cch, L"%s%s", dir, CONFIG_FILE) >= 0;
}

bool ConfigStore::GetProcessImagePath(const std::wstring& procName,
    const wchar_t* fileName,
    wchar_t* outPath, DWORD cch) const {
    wchar_t dir[MAX_PATH] = { 0 };
    if (!GetProcessDir(procName, dir, MAX_PATH, false)) return false;
    return swprintf_s(outPath, cch, L"%s%s", dir, fileName) >= 0;
}

// ---------- шаблон ----------
static const wchar_t* CONFIG_TEMPLATE =
L"; ============================================================\n"
L";  Game Overlay - configuration\n"
L"; ============================================================\n"
L";\n"
L"; ============================================================\n"
L";  1. HOTKEYS (defaults)\n"
L"; ============================================================\n"
L";\n"
L";   F8            add the active (foreground) application\n"
L";                 to the target process list (target_processes)\n"
L";\n"
L";   F7            remove the active application from the\n"
L";                 target process list\n"
L";\n"
L";   F9            show / hide the overlay manually\n"
L";\n"
L";   F10           edit mode:\n"
L";                   LMB on image and drag   = move\n"
L";                   mouse wheel over image  = zoom\n"
L";                   right edge              = width\n"
L";                   bottom edge             = height\n"
L";                   bottom-right corner     = proportional resize\n"
L";                   top-left corner         = rotate\n"
L";                     hold SHIFT            = snap to 15 deg\n"
L";                     double-click          = reset rotation to 0\n"
L";                   square near the top-right corner = toggle\n"
L";                     visibility of the image\n"
L";                   red cross on the right  = close overlay\n"
L";                 Press F10 again to leave edit mode.\n"
L";\n"
L";   F11           reset all images to default positions\n"
L";\n"
L";   Numpad *      reload images from disk\n"
L";                 (after replacing overlay_imgN.png)\n"
L";\n"
L"    Numpad /      Open the hotkey editor.\n"
L";                 To close the overlay: open the editor and press\n"
L";                 the red 'Exit' button (with confirmation).\n"
L";                 close the overlay (with confirmation)\n"
L";\n"
L";   To change a hotkey: edit the hex code in [Hotkeys] and\n"
L";   restart. Codes: F7=0x76 F8=0x77 F9=0x78 F10=0x79\n"
L";   F11=0x7A Numpad*=0x6A Numpad/=0x6F. Full VK list:\n"
L";   https://learn.microsoft.com/windows/win32/inputdev/virtual-key-codes\n"
L";\n"
L";   Per-process overrides:\n"
L";     <exe>\\<procname>\\overlay_config.ini  ->  [Hotkeys]\n"
L";     Same keys, hex VK codes. Missing keys fall back to this\n"
L";     global file, then to the built-in defaults.\n"
L";\n"
L"; ============================================================\n"
L";  2. TARGET APPLICATIONS\n"
L"; ============================================================\n"
L";\n"
L";   The overlay is shown ONLY when one of the applications\n"
L";   from target_processes is active. Otherwise it is hidden\n"
L";   (except in edit mode).\n"
L";\n"
L";   How to fill the list:\n"
L";     A) Start the game, Alt-Tab into it, press F8.\n"
L";        The program appends the process name here.\n"
L";        To remove: activate the app, press F7.\n"
L";     B) Manually below. Names are lowercase, exactly as in\n"
L";        Task Manager -> Details tab -> 'Image name' column.\n"
L";        Example: notepad.exe\n"
L";\n"
L";   Multiple applications are separated by semicolon ';'.\n"
L";   Example:  game.exe;launcher.exe\n"
L";\n"
L";   '*' (asterisk)   => overlay on top of ANY window.\n"
L";   '-' (minus)      => no targets, overlay always hidden.\n"
L";   Empty value is NOT supported: Win32 API\n"
L";   (WritePrivateProfileString) deletes the key when writing\n"
L";   an empty string, so the next launch restores defaults.\n"
L";\n"
L"; ============================================================\n"
L";  3. IMAGES\n"
L"; ============================================================\n"
L";\n"
L";   Slots 1-2 have BUILT-IN images (embedded into the .exe).\n"
L";   Slots 3-4 are empty by default.\n"
L";\n"
L";   To replace or add an image:\n"
L";     1. Place a PNG with alpha next to the .exe:\n"
L";          overlay_img1.png\n"
L";          overlay_img2.png\n"
L";          overlay_img3.png\n"
L";          overlay_img4.png\n"
L";     2. Press Numpad * (or restart the overlay).\n"
L";\n"
L";   A file next to the .exe takes priority over the embedded\n"
L";   image. If the file is absent, the embedded image is used\n"
L";   (slots 1-2) or the slot remains empty (3-4).\n"
L";\n"
L"; ============================================================\n"
L";  4. LAYOUT\n"
L"; ============================================================\n"
L";\n"
L";   Format:   imgN = x y w h rot vis\n"
L";\n"
L";     x, y   - top-left corner of the UNROTATED bounding box\n"
L";     w, h   - width and height of the UNROTATED bounding box\n"
L";     rot    - rotation in degrees, clockwise, 0..360\n"
L";              (drag the TOP-LEFT corner in edit mode;\n"
L";               hold SHIFT for 15 deg steps;\n"
L";               double-click the handle to reset to 0)\n"
L";     vis    - 1 = visible, 0 = hidden (toggle with the\n"
L";              small square near the top-right corner of\n"
L";              the image in edit mode). Optional, defaults to 1.\n"
L";\n"
L";   Units: DIP (logical pixels at 96 DPI), relative to the\n"
L";   top-left corner of the TARGET application's MONITOR\n"
L";   (not physical pixels - on monitors with 125%/150%\n"
L";   scaling they differ).\n"
L";\n"
L";   Values are overwritten automatically when leaving edit\n"
L";   mode (F10 off) and on exit.\n"
L";   You can edit them manually, but it is easier to move\n"
L";   the images with the mouse.\n"
L";\n"
L";   If the size is 0 0 0 0 - the slot is empty and not drawn.\n"
L";   The rot and vis fields may be omitted (default 0 and 1).\n"
L";\n"
L"; ============================================================\n"
L";  5. FAQ\n"
L"; ============================================================\n"
L";\n"
L";   Q: The overlay started but I see nothing.\n"
L";   A: Most likely the active application is not a target.\n"
L";      Switch to the game - the overlay will appear.\n"
L";\n"
L";   Q: The overlay does not react to the mouse.\n"
L";   A: That is expected outside edit mode -\n"
L";      clicks pass through to the game. Press F10.\n"
L";\n"
L";   Q: Where did my layout go?\n"
L";   A: If the file was missing, defaults are loaded.\n"
L";\n"
L";   Q: Can I run a second instance?\n"
L";   A: No, a second launch just shows the running one.\n"
L";\n"
L"; ============================================================\n"
L"\n";

void ConfigStore::EnsureConfigExists() const {
    wchar_t fullPath[MAX_PATH];
    if (!GetConfigPath(fullPath, MAX_PATH)) return;
    if (GetFileAttributesW(fullPath) != INVALID_FILE_ATTRIBUTES) return;

    HANDLE hFile = CreateFileW(fullPath, GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    const WORD bom = 0xFEFF;
    DWORD written = 0;
    WriteFile(hFile, &bom, sizeof(bom), &written, NULL);

    DWORD bytes = (DWORD)(wcslen(CONFIG_TEMPLATE) * sizeof(wchar_t));
    WriteFile(hFile, CONFIG_TEMPLATE, bytes, &written, NULL);
    CloseHandle(hFile);
}

// ---------- hotkeys ----------
std::wstring ConfigStore::FormatHotkey(UINT vk) {
    wchar_t buf[16];
    swprintf_s(buf, L"0x%X", vk);
    return buf;
}

UINT ConfigStore::ParseVkHex(const wchar_t* s, UINT def) {
    if (!s || !s[0]) return def;
    wchar_t* end = nullptr;
    unsigned long v = wcstoul(s, &end, 16);
    if (end == s) return def;
    if (v == 0 || v > 0xFF) return def;  
    return (UINT)v;
}

HotkeyConfig ConfigStore::DefaultHotkeys() {
    return HotkeyConfig{};
}

HotkeyConfig ConfigStore::LoadGlobalHotkeys() const {
    HotkeyConfig hk = DefaultHotkeys();

    wchar_t fullPath[MAX_PATH];
    if (!GetConfigPath(fullPath, MAX_PATH)) return hk;
    if (GetFileAttributesW(fullPath) == INVALID_FILE_ATTRIBUTES) return hk;

    for (int i = 0; i < kHotkeyFieldDescCount; ++i) {
        wchar_t buf[32] = { 0 };
        GetPrivateProfileStringW(L"Hotkeys", kHotkeyFieldDescs[i].iniKey, L"",
            buf, 32, fullPath);
        UINT& slot = hk.*(kHotkeyFieldDescs[i].member);
        slot = ParseVkHex(buf, slot);
    }
    return hk;
}

HotkeyConfig ConfigStore::LoadHotkeysForProcess(const std::wstring& procName) const {
    HotkeyConfig hk = LoadGlobalHotkeys();

    if (procName.empty()) return hk;

    wchar_t procPath[MAX_PATH] = { 0 };
    if (!GetProcessConfigPath(procName, procPath, MAX_PATH, /*create*/ false))
        return hk;
    if (GetFileAttributesW(procPath) == INVALID_FILE_ATTRIBUTES)
        return hk;

    for (int i = 0; i < kHotkeyFieldDescCount; ++i) {
        wchar_t buf[32] = { 0 };
        GetPrivateProfileStringW(L"Hotkeys", kHotkeyFieldDescs[i].iniKey, L"",
            buf, 32, procPath);
        if (buf[0]) {
            UINT& slot = hk.*(kHotkeyFieldDescs[i].member);
            slot = ParseVkHex(buf, slot);
        }
    }
    return hk;
}

bool ConfigStore::SaveHotkeysForProcess(const std::wstring& procName,
    const HotkeyConfig& hk, bool onlyIfEmpty) const
{
    if (procName.empty()) return false;

    wchar_t fullPath[MAX_PATH] = { 0 };
    if (!GetProcessConfigPath(procName, fullPath, MAX_PATH, /*create*/ true)) {
        OutputDebugStringW(L"[ConfigStore] SaveHotkeysForProcess: GetProcessConfigPath failed\n");
        return false;
    }

    if (onlyIfEmpty && GetFileAttributesW(fullPath) != INVALID_FILE_ATTRIBUTES) {
        bool hasAny = false;
        for (int i = 0; i < kHotkeyFieldDescCount && !hasAny; ++i) {
            wchar_t probe[8] = { 0 };
            GetPrivateProfileStringW(L"Hotkeys", kHotkeyFieldDescs[i].iniKey, L"",
                probe, 8, fullPath);
            if (probe[0]) hasAny = true;
        }
        if (hasAny) return true;
    }

    bool allOk = true;
    for (int i = 0; i < kHotkeyFieldDescCount; ++i) {
        std::wstring val = FormatHotkey(hk.*(kHotkeyFieldDescs[i].member));
        if (!WritePrivateProfileStringW(L"Hotkeys", kHotkeyFieldDescs[i].iniKey,
            val.c_str(), fullPath))
        {
            OutputDebugStringW(L"[ConfigStore] SaveHotkeysForProcess: write failed\n");
            allOk = false;
        }
    }
    return allOk;
}

// ---------- strings ----------
std::wstring ConfigStore::TrimAndLower(std::wstring s) {
    size_t b = s.find_first_not_of(L" \t\r\n");
    size_t e = s.find_last_not_of(L" \t\r\n");
    if (b == std::wstring::npos) return std::wstring();
    s = s.substr(b, e - b + 1);
    if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
    return s;
}

std::vector<std::wstring> ConfigStore::ParseProcessList(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == L';' || c == L',') {
            std::wstring t = TrimAndLower(cur);
            if (!t.empty()) out.push_back(std::move(t));
            cur.clear();
        }
        else {
            cur += c;
        }
    }
    {
        std::wstring t = TrimAndLower(cur);
        if (!t.empty()) out.push_back(std::move(t));
    }
    return out;
}

// ---------- layout parsing ----------
int ConfigStore::ParseLayoutLine(const wchar_t* s,
    int& x, int& y, int& w, int& h, float& rot, int& vis)
{
    if (!s) return 0;
    const wchar_t* p = s;
    auto skipWs = [&]() { while (*p == L' ' || *p == L'\t') ++p; };

    auto atFieldBoundary = [](const wchar_t* end) {
        return (*end == 0 || *end == L' ' || *end == L'\t' ||
            *end == L'\r' || *end == L'\n');
        };

    auto parseInt = [&](int& out) -> bool {
        skipWs();
        if (!*p) return false;
        wchar_t* end = nullptr;
        long v = wcstol(p, &end, 10);
        if (end == p) return false;
        if (!atFieldBoundary(end)) return false; 
        out = (int)v; p = end; return true;
        };
    auto parseFloat = [&](float& out) -> bool {
        skipWs();
        if (!*p) return false;
        wchar_t* end = nullptr;
        double v = wcstod(p, &end);
        if (end == p) return false;
        if (!atFieldBoundary(end)) return false;
        out = (float)v; p = end; return true;
        };

    int n = 0;
    if (!parseInt(x)) return n; ++n;
    if (!parseInt(y)) return n; ++n;
    if (!parseInt(w)) return n; ++n;
    if (!parseInt(h)) return n; ++n;
    if (!parseFloat(rot)) return n; ++n;
    if (!parseInt(vis)) return n; ++n;
    vis = (vis != 0) ? 1 : 0;
    return n;
}

// ---------- global ----------
TargetPolicy ConfigStore::LoadGlobalConfig() const {
    TargetPolicy targets;

    wchar_t fullPath[MAX_PATH];
    if (!GetConfigPath(fullPath, MAX_PATH)) return targets;
    if (GetFileAttributesW(fullPath) == INVALID_FILE_ATTRIBUTES) return targets;

    wchar_t procsBuf[1024] = { 0 };
    GetPrivateProfileStringW(L"General", L"target_processes", L"\x01",
        procsBuf, 1024, fullPath);

    if (procsBuf[0] == L'\x01') {
    }
    else if (wcscmp(procsBuf, TARGET_ANY_MARKER) == 0) {
        targets.showEverywhere = true;
    }
    else if (wcscmp(procsBuf, TARGET_NONE_MARKER) == 0) {
    }
    else {
        targets.processes = ParseProcessList(procsBuf);
        std::vector<std::wstring> uniq;
        uniq.reserve(targets.processes.size());
        for (auto& p : targets.processes) {
            bool dup = false;
            for (auto& u : uniq)
                if (_wcsicmp(u.c_str(), p.c_str()) == 0) { dup = true; break; }
            if (!dup) uniq.push_back(p);
        }
        targets.processes.swap(uniq);
    }
    return targets;
}

bool ConfigStore::SaveGlobalConfig(const TargetPolicy& targets,
    std::span<const HotkeyDef> hotkeys) const
{
    wchar_t fullPath[MAX_PATH];
    if (!GetConfigPath(fullPath, MAX_PATH)) {
        OutputDebugStringW(L"[ConfigStore] SaveGlobalConfig: GetConfigPath failed\n");
        return false;
    }

    std::wstring procs;
    if (targets.showEverywhere) {
        procs = TARGET_ANY_MARKER;
    }
    else {
        for (size_t i = 0; i < targets.processes.size(); ++i) {
            if (i) procs += L';';
            procs += targets.processes[i];
        }
        if (procs.empty()) procs = TARGET_NONE_MARKER;
    }

    BOOL ok = WritePrivateProfileStringW(L"General", L"target_processes",
        procs.c_str(), fullPath);
    if (!ok) OutputDebugStringW(L"[ConfigStore] SaveGlobalConfig: target_processes write failed\n");

    for (const auto& hk : hotkeys) {
        if (!WritePrivateProfileStringW(L"Hotkeys", hk.iniKey,
            FormatHotkey(hk.vk).c_str(), fullPath))
        {
            OutputDebugStringW(L"[ConfigStore] SaveGlobalConfig: hotkey write failed\n");
        }
    }
    return ok != FALSE;
}

// ---------- layout ----------
void ConfigStore::ResetToDefaults(std::vector<OverlayImage>& images) {
    for (int i = 0; i < (int)images.size() && i < MAX_IMAGES; ++i) {
        OverlayImage& img = images[i];
        img.x = DEFAULT_LAYOUT[i].x;
        img.y = DEFAULT_LAYOUT[i].y;
        img.rotation = 0.0f;
        img.visible = true;
        SetImageWidthKeepAspect(img, DEFAULT_LAYOUT[i].w);
        if (img.height <= 0) img.height = DEFAULT_LAYOUT[i].w;
    }
}

bool ConfigStore::LoadLayoutForProcess(const std::wstring& procName,
    std::vector<OverlayImage>& images) const
{
    ResetToDefaults(images);
    if (procName.empty()) return false;

    wchar_t fullPath[MAX_PATH] = { 0 };
    if (!GetProcessConfigPath(procName, fullPath, MAX_PATH, false)) return false;
    if (GetFileAttributesW(fullPath) == INVALID_FILE_ATTRIBUTES) return false;

    for (int i = 0; i < (int)images.size() && i < MAX_IMAGES; ++i) {
        wchar_t buf[160] = { 0 };
        GetPrivateProfileStringW(L"Images", kSlots[i].iniKey, L"",
            buf, 160, fullPath);

        int x = 0, y = 0, w = 0, h = 0, vis = 1;
        float rot = 0.0f;
        int n = ParseLayoutLine(buf, x, y, w, h, rot, vis);
        if (n >= 4) {
            int cap = EffectiveMaxSize();
            bool validWH = (w >= MIN_SIZE && w <= cap &&
                h >= MIN_SIZE && h <= cap);
            bool validXY = (x >= -100000 && x <= 100000 &&
                y >= -100000 && y <= 100000);
            if (validWH && validXY) {
                images[i].x = x; images[i].y = y;
                images[i].width = w; images[i].height = h;
                if (n >= 5) {
                    while (rot < 0.0f) rot += 360.0f;
                    while (rot >= 360.0f) rot -= 360.0f;
                    images[i].rotation = rot;
                }
                if (n >= 6) images[i].visible = (vis != 0);
            }
            else {
                wchar_t line[256];
                swprintf_s(line,
                    L"[ConfigStore] LoadLayoutForProcess: slot '%s' has invalid "
                    L"values (x=%d y=%d w=%d h=%d), keeping defaults\n",
                    kSlots[i].iniKey, x, y, w, h);
                OutputDebugStringW(line);
            }
        }
    }
    return true;
}

bool ConfigStore::SaveLayoutForProcess(const std::wstring& procName,
    const std::vector<OverlayImage>& images,
    bool onlyIfEmpty) const
{
    if (procName.empty()) return false;

    wchar_t fullPath[MAX_PATH] = { 0 };
    if (!GetProcessConfigPath(procName, fullPath, MAX_PATH, /*create*/ true)) {
        OutputDebugStringW(L"[ConfigStore] SaveLayoutForProcess: GetProcessConfigPath failed\n");
        return false;
    }

    // Если файл уже есть и содержит любой layout — при onlyIfEmpty не перезаписываем.
    if (onlyIfEmpty && GetFileAttributesW(fullPath) != INVALID_FILE_ATTRIBUTES) {
        bool hasAny = false;
        for (int i = 0; i < MAX_IMAGES && !hasAny; ++i) {
            wchar_t probe[8] = { 0 };
            GetPrivateProfileStringW(L"Images", kSlots[i].iniKey, L"",
                probe, 8, fullPath);
            if (probe[0]) hasAny = true;
        }
        if (hasAny) return true;
    }

    bool allOk = true;
    if (onlyIfEmpty) {
        for (int i = 0; i < MAX_IMAGES; ++i) {
            int w = DEFAULT_LAYOUT[i].w;
            int h = w;
            if (i < (int)images.size() &&
                images[i].nativeW > 0 && images[i].nativeH > 0)
            {
                h = (int)std::lround(
                    (double)w * images[i].nativeH / images[i].nativeW);
                if (h < MIN_SIZE) h = MIN_SIZE;
            }
            wchar_t buf[160];
            swprintf_s(buf, L"%d %d %d %d %.2f %d",
                DEFAULT_LAYOUT[i].x, DEFAULT_LAYOUT[i].y, w, h, 0.0f, 1);
            if (!WritePrivateProfileStringW(L"Images", kSlots[i].iniKey, buf, fullPath)) {
                OutputDebugStringW(L"[ConfigStore] SaveLayoutForProcess: write failed\n");
                allOk = false;
            }
        }
        return allOk;
    }

    for (int i = 0; i < (int)images.size() && i < MAX_IMAGES; ++i) {
        const auto& img = images[i];
        wchar_t buf[160];
        swprintf_s(buf, L"%d %d %d %d %.2f %d",
            img.x, img.y, img.width, img.height, img.rotation,
            img.visible ? 1 : 0);
        if (!WritePrivateProfileStringW(L"Images", kSlots[i].iniKey, buf, fullPath)) {
            OutputDebugStringW(L"[ConfigStore] SaveLayoutForProcess: write failed\n");
            allOk = false;
        }
    }
    return allOk;
}

bool ConfigStore::SaveAll(const TargetPolicy& targets,
    std::span<const HotkeyDef> hotkeys,
    const std::wstring& layoutOwner,
    bool layoutDirty,
    const std::vector<OverlayImage>& images) const
{
    bool a = SaveGlobalConfig(targets, hotkeys);
    bool b = true;
    if (layoutDirty && !layoutOwner.empty())
        b = SaveLayoutForProcess(layoutOwner, images, /*onlyIfEmpty*/ false);
    return a && b;
}