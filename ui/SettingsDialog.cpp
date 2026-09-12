// SettingsDialog.cpp - preferences dialog implementation
#include "ui/SettingsDialog.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "core/ShortcutCore.h"
#include "ui/ThemeWidgets.h"
#include "ui/SmokeTest.h"
#include "core/Config.h"

#include <FL/Fl.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#include <string.h>
#include <string>
#include <vector>
#include <shlobj.h>    // SHChangeNotify

#if defined(_WIN32)
#include <windows.h>
#endif

// A scroll group that draws its scrollbar in the same flat style as the
// main-editor custom scrollbar (Editor::drawCustomScrollbar), instead of
// the FLTK native scrollbar appearance.
class SettingsScroll : public Fl_Scroll {
    const Theme *m_theme;
public:
    SettingsScroll(int X, int Y, int W, int H, const Theme *theme)
        : Fl_Scroll(X, Y, W, H), m_theme(theme) {}

    void draw() override {
        // Force full redraw — fl_scroll (BitBlt) moves pixels in the
        // window DC, displacing custom borders.  damage(FL_DAMAGE_ALL)
        // makes Fl_Scroll take the draw_box+draw_clip path instead.
        damage(FL_DAMAGE_ALL);
        Fl_Scroll::draw();

        // Overlay custom scrollbar matching main window style.
        Fl_Scrollbar &sb = scrollbar;
        if (!sb.visible_r()) return;

        int sx = sb.x();
        int sy = sb.y();
        int sw = sb.w();
        int sh = sb.h();

        double minv = sb.minimum();
        double maxv = sb.maximum();
        double val  = sb.value();
        float  sl   = sb.slider_size();

        Fl_Color trackColor, thumbColor;
        if (m_theme) scrollbarColors(m_theme->colors(), trackColor, thumbColor);
        else { trackColor = fl_rgb_color(240, 240, 240); thumbColor = fl_rgb_color(180, 180, 180); }

        // Track
        ::fl_color(trackColor);
        ::fl_rectf(sx, sy, sw, sh);

        // Thumb (vertical only)
        int thumb_h = (int)(sh * sl);
        if (thumb_h < 20) thumb_h = 20;
        int thumb_y = sy;
        if (maxv > minv)
            thumb_y = sy + (int)((val - minv) * (sh - thumb_h) / (maxv - minv));
        ::fl_color(thumbColor);
        ::fl_rectf(sx, thumb_y, sw, thumb_h);
    }
};

// (SettingsChoice moved to ThemeWidgets.h - shared with ParamDialog.)

// Registry helpers for system integration.
// Full list of extensions Pecia can register itself for in the "Open with"
// menu. Kept in a stable display order so the ExtensionsDialog checkboxes
// read naturally. All entries are plain-text formats Pecia can edit safely.
// Web files (.html/.htm/.css/.js) and source code (.c/.cpp/.java/...) are
// intentionally excluded - users expect those in browsers or IDEs, and
// Pecia's minimal nature doesn't fit their editing workflow.
static const wchar_t *kAssocExts[] = {
    // Plain text & data
    L".txt", L".text", L".md", L".log", L".csv", L".tsv",
    // Configuration
    L".ini", L".cfg", L".conf", L".properties", L".yaml", L".yml",
    // Data serialization
    L".json", L".xml",
    // Scripts
    L".bat", L".cmd", L".ps1", L".sh",
    // Other plain text
    L".tex", L".srt"
};
static const int kAssocExtCount = sizeof(kAssocExts) / sizeof(kAssocExts[0]);

// Split a comma-separated extension list into individual entries.
// Each entry is returned as a std::wstring including the leading dot.
// Empty entries (e.g. from "a,,b" or a trailing comma) are skipped.
static std::vector<std::wstring> splitExts(const std::wstring &csv) {
    std::vector<std::wstring> out;
    size_t start = 0;
    while (start <= csv.size()) {
        size_t end = csv.find(L',', start);
        std::wstring tok = (end == std::wstring::npos)
            ? csv.substr(start)
            : csv.substr(start, end - start);
        // Trim whitespace.
        while (!tok.empty() && (tok.front() == L' ' || tok.front() == L'\t')) tok.erase(0, 1);
        while (!tok.empty() && (tok.back()  == L' ' || tok.back()  == L'\t')) tok.pop_back();
        if (!tok.empty()) out.push_back(tok);
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return out;
}

// Join a list of extensions back into a comma-separated string.
static std::wstring joinExts(const std::vector<std::wstring> &exts) {
    std::wstring out;
    for (size_t i = 0; i < exts.size(); ++i) {
        if (i) out += L",";
        out += exts[i];
    }
    return out;
}

static std::wstring getExePathW() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(L"\"") + buf + L"\" \"%1\"";
}

// Returns the bare executable path without arguments, e.g. "C:\path\Pecia.exe"

// Returns just the exe file name, e.g. "Pecia.exe".
static std::wstring getExeNameW() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    size_t pos = path.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? path.substr(pos + 1) : path;
}

// --- Explorer OpenWithList (first-level "Open with" submenu) ---
// Windows Explorer builds the right-click "Open with" submenu from
// HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.ext\
// OpenWithList, NOT from Software\Classes\.ext\OpenWithProgids. The latter
// only feeds the "Choose another app" dialog. To appear in the first-level
// submenu, we must add our exe name to the OpenWithList MRU list.
//
// The format: single-letter value names (a, b, c, ...) map to exe names,
// and "MRUList" is a string of those letters in most-recent-first order.

// Add exeName to the OpenWithList for the extension. Assigns the next
// available letter and appends it to MRUList.
static void addToOpenWithList(const std::wstring &ext, const std::wstring &exeName, DWORD sam) {
    std::wstring basePath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\"
                           + ext + L"\\OpenWithList";
    HKEY hk;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, basePath.c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE | sam,
                        nullptr, &hk, nullptr) != ERROR_SUCCESS)
        return;

    // Scan existing values: find if exeName is already listed, track used
    // letters, and read the current MRUList.
    bool used[26] = {};
    wchar_t existingLetter = 0;
    wchar_t mruList[64] = {};
    DWORD mruSz = sizeof(mruList);
    RegQueryValueExW(hk, L"MRUList", nullptr, nullptr, (LPBYTE)mruList, &mruSz);

    wchar_t nameBuf[2]; DWORD nameSz;
    wchar_t valBuf[MAX_PATH]; DWORD valSz; DWORD type;
    for (int i = 0; ; ++i) {
        nameSz = 2; valSz = sizeof(valBuf);
        if (RegEnumValueW(hk, i, nameBuf, &nameSz, nullptr, &type,
                          (LPBYTE)valBuf, &valSz) != ERROR_SUCCESS) break;
        if (nameSz == 0 || wcslen(nameBuf) == 0) continue;
        wchar_t ch = nameBuf[0];
        if (ch >= L'a' && ch <= L'z') used[ch - L'a'] = true;
        if (_wcsicmp(valBuf, exeName.c_str()) == 0) existingLetter = ch;
    }

    // If not listed yet, assign the next free letter and write the value.
    wchar_t letter = existingLetter;
    if (letter == 0) {
        for (int i = 0; i < 26; ++i) {
            if (!used[i]) { letter = (wchar_t)(L'a' + i); break; }
        }
        if (letter) {
            wchar_t nm[2] = { letter, 0 };
            RegSetValueExW(hk, nm, 0, REG_SZ, (const BYTE*)exeName.c_str(),
                           (DWORD)((exeName.size() + 1) * sizeof(wchar_t)));
        }
    }

    // Ensure the letter appears in MRUList so Explorer shows it.
    if (letter && !wcschr(mruList, letter)) {
        std::wstring newMru(mruList);
        newMru += letter;
        RegSetValueExW(hk, L"MRUList", 0, REG_SZ, (const BYTE*)newMru.c_str(),
                       (DWORD)((newMru.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(hk);
}

static bool regKeyExists(HKEY hive, const wchar_t *subKey, const wchar_t *value = nullptr) {
    HKEY hk = nullptr;
    LONG ret = RegOpenKeyExW(hive, subKey, 0, KEY_READ | KEY_WOW64_64KEY, &hk);
    if (ret != ERROR_SUCCESS) return false;
    if (!value) { RegCloseKey(hk); return true; }
    wchar_t buf[2] = { 0 };
    DWORD sz = sizeof(buf);
    ret = RegQueryValueExW(hk, value, nullptr, nullptr, (LPBYTE)buf, &sz);
    RegCloseKey(hk);
    return ret == ERROR_SUCCESS;
}

// Apply / remove the "New > Text Document" shell entry.
static void applyNewTxtRegistry(bool doNewTxt) {
    auto regWrite = [&](DWORD sam) {
        if (doNewTxt) {
            HKEY hk;

            // Keep .txt ProgID as txtfile (don't override with Pecia.txt)
            if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.txt", 0, nullptr,
                                REG_OPTION_NON_VOLATILE, KEY_WRITE | sam, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
                RegSetValueExW(hk, nullptr, 0, REG_SZ, (const BYTE*)L"txtfile",
                               sizeof(L"txtfile"));  // includes null terminator, in bytes
                RegCloseKey(hk);
            }
            // Add ShellNew\NullFile (standard way, Command causes issues)
            if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.txt\\ShellNew", 0, nullptr,
                                REG_OPTION_NON_VOLATILE, KEY_WRITE | sam, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
                RegSetValueExW(hk, L"NullFile", 0, REG_SZ, (const BYTE*)L"", 2);
                RegCloseKey(hk);
            }

            // Update the ShellNew cache so Explorer picks up the change immediately.
            if (RegCreateKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Discardable\\PostSetup\\ShellNew",
                    0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE | sam, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
                // Read existing Classes value, append ".txt"
                wchar_t buf[4096] = {};
                DWORD sz = sizeof(buf);
                RegQueryValueExW(hk, L"Classes", nullptr, nullptr, (LPBYTE)buf, &sz);
                std::wstring classes(buf, sz / 2);
                // Remove trailing nulls
                while (!classes.empty() && classes.back() == L'\0') classes.pop_back();
                if (classes.find(L".txt") == std::wstring::npos) {
                    if (!classes.empty()) classes += L" ";
                    classes += L".txt";
                }
                RegSetValueExW(hk, L"Classes", 0, REG_SZ, (const BYTE*)classes.c_str(),
                               (DWORD)((classes.size() + 1) * sizeof(wchar_t)));
                RegCloseKey(hk);
            }

            // Override txtfile's FriendlyTypeName. HKLM has a malformed value
            // "txt.file" (set by some other app) which makes new files named
            // "新建 txt.file.txt". HKCU overrides HKLM, but the override
            // value must NOT be empty - an empty FriendlyTypeName makes
            // Explorer silently drop the "New" menu entry. Use a plain
            // ASCII string so the new file name is "新建 Text Document.txt"
            // regardless of system language (resource strings like
            // "@notepad.exe,-123" can render as garbled text on some systems).
            if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\txtfile", 0, nullptr,
                                REG_OPTION_NON_VOLATILE, KEY_WRITE | sam, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
                const wchar_t *kFriendly = L"Text Document";
                RegSetValueExW(hk, L"FriendlyTypeName", 0, REG_SZ,
                               (const BYTE*)kFriendly,
                               (DWORD)((wcslen(kFriendly) + 1) * sizeof(wchar_t)));
                RegCloseKey(hk);
            }
        } else {
            // Cleanup: remove ShellNew, reset .txt to txtfile
            RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\.txt\\ShellNew");
            HKEY hk;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.txt", 0, KEY_SET_VALUE | sam, &hk) == ERROR_SUCCESS) {
                RegSetValueExW(hk, nullptr, 0, REG_SZ, (const BYTE*)L"txtfile",
                               sizeof(L"txtfile"));  // includes null terminator, in bytes
                RegCloseKey(hk);
            }
            // Remove our FriendlyTypeName override so HKLM's value is used again.
            RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\Classes\\txtfile", L"FriendlyTypeName");
        }
    };

    regWrite(0);
    regWrite(KEY_WOW64_64KEY);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

// Add Pecia to the "Open with" menu for the given extensions.
// This is an add-only operation - existing entries are never removed.
// Two mechanisms work together:
//  1) Explorer OpenWithList (Explorer\FileExts\.ext\OpenWithList):
//     This is what Windows reads to build the FIRST-LEVEL "Open
//     with" submenu. We add "Pecia.exe" to the MRU list for each
//     selected extension.
//  2) OpenWithProgids (Software\Classes\.ext\OpenWithProgids):
//     This feeds the "Choose another app" dialog. We register a
//     ProgID "Pecia" with our shell\open\command and add a REG_NONE
//     entry for each extension. Works for ALL extensions, even
//     those with no existing file association (e.g. .py, .srt).
// We also keep Applications\Pecia.exe as a fallback for the
// "Choose another app" dialog.
static void applyOpenWithRegistry(const std::wstring &openWithExts) {
    std::wstring exeCmd = getExePathW();
    std::wstring exeName = getExeNameW();
    std::vector<std::wstring> exts = splitExts(openWithExts);

    auto regWrite = [&](DWORD sam) {
        HKEY hk;
        // Register the ProgID command line.
        if (RegCreateKeyExW(HKEY_CURRENT_USER,
                            L"Software\\Classes\\Pecia\\shell\\open\\command",
                            0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE | sam, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(hk, nullptr, 0, REG_SZ, (const BYTE*)exeCmd.c_str(),
                           (DWORD)((exeCmd.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hk);
        }
        // Also register under Applications\Pecia.exe (used by the
        // "Choose another app" dialog as a fallback).
        if (RegCreateKeyExW(HKEY_CURRENT_USER,
                            L"Software\\Classes\\Applications\\Pecia.exe\\shell\\open\\command",
                            0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE | sam, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(hk, nullptr, 0, REG_SZ, (const BYTE*)exeCmd.c_str(),
                           (DWORD)((exeCmd.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hk);
        }
        // Add OpenWithProgids entries for the selected extensions.
        // REG_NONE with zero bytes is the standard type used by Windows
        // and other apps (Positron, QoderCN, etc.) for these entries.
        for (const auto &ext : exts) {
            std::wstring key = L"Software\\Classes" + ext + L"\\OpenWithProgids";
            HKEY extHk;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr,
                                REG_OPTION_NON_VOLATILE, KEY_WRITE | sam, nullptr, &extHk, nullptr) == ERROR_SUCCESS) {
                RegSetValueExW(extHk, L"Pecia", 0, REG_NONE, nullptr, 0);
                RegCloseKey(extHk);
            }
        }
        // Add Pecia.exe to the Explorer OpenWithList for each selected
        // extension. This is what makes Pecia appear in the FIRST-LEVEL
        // "Open with" submenu (right-click → Open with → Pecia).
        // OpenWithProgids alone only puts us in "Choose another app".
        for (const auto &ext : exts) {
            addToOpenWithList(ext, exeName, sam);
        }
    };

    regWrite(0);
    regWrite(KEY_WOW64_64KEY);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

// Tab widths offered in the settings dialog (in spaces).
// The INI value is constrained to one of these; any other value
// read back from settings.ini falls back to 4 (the default).
static const int g_tabWidths[] = { 2, 4, 8 };
static const int g_tabWidthCount = sizeof(g_tabWidths) / sizeof(g_tabWidths[0]);

// Height of the custom-drawn title bar (mirrors InfoWindow / main window).
SettingsDialog::SettingsDialog(int w, int h, const char *title, const Theme *theme,
                       int uiFontSize, int pad)
    : DialogBase(w, h + TITLE_H, title, theme, uiFontSize, ModalDialog), m_theme(theme), m_uiFontSize(uiFontSize), m_pad(pad) {
    begin();
    initShell(title);

    int margin = 12;
    int rowH = 24;

    // Size the dialog so every setting row fits without scrolling. Row
    // counts must match the actual controls below (currently 14: edit 5,
    // save&exit 6, interface&system 3 - one of them with a +4 gap),
    // plus 3 group titles and 2 half blank rows between groups.
    int rowsH = 13 * (rowH + 6) + (rowH + 4) + 3 * (rowH - 2) + 2 * (rowH / 2);
    // Add m_pad top/bottom padding (outside the scroll) plus the
    // bottom button bar (gBarH).
    int needH = rowsH + 2 * m_pad + gBarH;
    if (h < needH) {
        h = needH;
        size(w, h + TITLE_H);
    }

    int labelW = 110;
    int ctrlW = w - margin * 2 - labelW - 20;

    int btnH = gBtnH;
    // Bottom button bar: same height as the title bar, buttons centered.
    // Flush to the very bottom of the window.
    int btnBarY = TITLE_H + h - gBarH;      // bottom bar top (window coords)
    // Scroll area sits below a fixed m_pad gap (outside the scroll, so
    // it does NOT scroll away with the content).
    int scrollY = TITLE_H + m_pad;
    int scrollH = btnBarY - scrollY;           // fill remaining content area
    Fl_Scroll *scroll = new SettingsScroll(0, scrollY, w, scrollH, theme);
    scroll->type(Fl_Scroll::VERTICAL);
    scroll->box(FL_FLAT_BOX);
    scroll->color(theme ? theme->colors().background1 : FL_WHITE);

    // Content Y starts flush at the scroll top; the m_pad gap above is
    // outside the scroll and therefore fixed (doesn't scroll away).
    int y = scrollY;

    // Group title row: left-aligned, bold, secondary color, slightly
    // smaller than the option text. A blank row separates each group
    // from the previous one (except the first, which follows the dialog
    // padding).
    bool firstTitle = true;
    auto addGroupTitle = [&](const char *label) {
        if (!firstTitle) y += rowH / 2;   // 半行间距分隔分组（整行偏高，标题样式已足够区分）
        firstTitle = false;
        int ty = y + 3;
        Fl_Box *t = new Fl_Box(FL_NO_BOX, margin, ty, w - 2 * margin, rowH - 6,
                               label);
        t->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        t->labelsize(uiFontSize > 2 ? uiFontSize - 2 : uiFontSize);
        t->labelfont(FL_HELVETICA_BOLD);
        t->labelcolor(theme ? theme->colors().text2
                            : fl_rgb_color(120, 120, 120));
        t->user_data((void*)1);   // group-title marker: skip right-align pass
        y += rowH - 2;
    };

    // ---------- Editing ----------
    addGroupTitle(I18n::get("settings.group_edit"));

    // --- Tab width (2 / 4 / 8 spaces) ---
    new Fl_Box(FL_NO_BOX, margin, y, labelW, rowH, I18n::get("settings.tabwidth"));
    m_tabWidthChoice = new SettingsChoice(margin + labelW, y, 80, rowH);
    for (int i = 0; i < g_tabWidthCount; ++i) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", g_tabWidths[i]);
        m_tabWidthChoice->add(buf);
    }
    y += rowH + 6;

    // --- Long line marker (0 = off, >0 = column) ---
    new Fl_Box(FL_NO_BOX, margin, y, labelW, rowH, I18n::get("settings.longline"));
    m_longLineInput = new Fl_Int_Input(margin + labelW, y, 80, rowH);
    m_longLineInput->value("0");
    y += rowH + 6;

    // --- Search engine URL template (%s = selection) ---
    new Fl_Box(FL_NO_BOX, margin, y, labelW, rowH, I18n::get("settings.searchurl"));
    m_searchUrlInput = new Fl_Input(margin + labelW, y, ctrlW, rowH);
    y += rowH + 6;

    // --- Auto indent（编辑行为，属编辑分组）---
    m_autoIndentChk = new Fl_Check_Button(margin + labelW, y, ctrlW, rowH,
                                            I18n::get("settings.autoindent"));
    y += rowH + 6;

    // --- Detect URLs（编辑行为，属编辑分组）---
    m_detectUrlsChk = new Fl_Check_Button(margin + labelW, y, ctrlW, rowH,
        I18n::get("settings.detecturls"));
    y += rowH + 6;

    // --- Auto-save interval ---
    addGroupTitle(I18n::get("settings.group_save"));
    new Fl_Box(FL_NO_BOX, margin, y, labelW, rowH, I18n::get("settings.autosave"));
    m_autoSaveChoice = new SettingsChoice(margin + labelW, y, 120, rowH);
    m_autoSaveChoice->add(I18n::get("settings.autosaveitems"));
    y += rowH + 6;

    // --- Clean temp files older than 7 days (exit) ---
    m_cleanupTempChk = new Fl_Check_Button(margin + labelW, y, ctrlW, rowH,
        I18n::get("settings.cleanuptemp"));
    y += rowH + 6;

    // --- Trim trailing whitespace on save ---
    m_trimTrailingChk = new Fl_Check_Button(margin + labelW, y, ctrlW, rowH,
                                              I18n::get("settings.trimtrailing"));
    y += rowH + 6;

    // --- Trim leading blank (document start) ---
    m_trimLeadingChk = new Fl_Check_Button(margin + labelW, y, ctrlW, rowH,
        I18n::get("settings.trimleading"));
    y += rowH + 6;

    // --- Trim ending blank (document end) ---
    m_trimEndingChk = new Fl_Check_Button(margin + labelW, y, ctrlW, rowH,
        I18n::get("settings.trimending"));
    y += rowH + 6;

    // --- Expand tabs on save ---
    m_expandTabsChk = new Fl_Check_Button(margin + labelW, y, ctrlW, rowH,
        I18n::get("settings.expandtabs"));
    y += rowH + 6;

    // ---------- Interface ----------
    addGroupTitle(I18n::get("settings.group_ui_sys"));

    // --- Multi tab ---
    m_multiTabChk = new Fl_Check_Button(margin + labelW, y, ctrlW, rowH,
                                          I18n::get("settings.multitab"));
    y += rowH + 6;

    // --- System Integration ---
    m_integNewTxt = new Fl_Check_Button(margin + labelW, y, ctrlW, rowH,
        I18n::get("settings.newtxtdoc"));
    y += rowH + 4;
    // "Add to file Open with menu" button opens the ExtensionsDialog.
    // Width auto-fits the label text (so it adapts to language changes),
    // left-aligned with the other checkboxes (same X as Multi tab etc.).
    // Label is centered inside.
    {
        fl_font(FL_HELVETICA, uiFontSize);
        const char *kLabel = I18n::get("settings.addopenwith");
        int txtW = (int)(fl_width(kLabel) + 0.5);
        int btnW = txtW + 16;  // 8px padding on each side
        int btnX = margin + labelW;  // left-align with checkboxes
        m_chooseExtsBtn = new HoverButton(btnX, y, btnW, rowH, kLabel);
    }
    m_chooseExtsBtn->callback(cbChooseExts, this);
    m_chooseExtsBtn->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    y += rowH + 6;

    // Bottom spacer inside the scroll content: the last row sits m_pad
    // above the scroll bottom (= button bar top), symmetric with the
    // fixed top gap below the title bar.
    new Fl_Box(FL_NO_BOX, 0, y, 0, m_pad, nullptr);

    scroll->end();

    // Uniform font size and primary text color for all controls
    Fl_Color fg = theme ? theme->colors().text1 : FL_BLACK;
    for (int i = 0; i < scroll->children(); ++i) {
        auto *wdg = scroll->child(i);
        wdg->labelsize(uiFontSize);
        wdg->labelcolor(fg);
    }

    // Right-align label boxes (x == margin, box type FL_NO_BOX). Group
    // titles are marked via user_data and stay left-aligned.
    for (int i = 0; i < scroll->children(); ++i) {
        auto *wdg = scroll->child(i);
        if (wdg->x() == margin && dynamic_cast<Fl_Box *>(wdg) &&
            wdg->user_data() == nullptr)
            wdg->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
    }

    // Style all checkboxes — same as FindReplace "Match case".
    Fl_Color chkBg = theme ? theme->colors().background1 : FL_WHITE;
    Fl_Color chkFg = theme ? theme->colors().text1 : FL_BLACK;
    auto styleChk = [&](Fl_Check_Button *cb) {
        cb->box(FL_NO_BOX);
        cb->down_box(FL_BORDER_BOX);
        cb->color(chkBg);
        cb->selection_color(theme ? theme->colors().highlight2 : FL_SELECTION_COLOR);
        cb->labelcolor(chkFg);
        cb->labelsize(uiFontSize);
    };
    styleChk(m_autoIndentChk);
    styleChk(m_multiTabChk);
    styleChk(m_trimTrailingChk);
    styleChk(m_trimLeadingChk);
    styleChk(m_trimEndingChk);
    styleChk(m_expandTabsChk);
    styleChk(m_integNewTxt);
    styleChk(m_detectUrlsChk);
    styleChk(m_cleanupTempChk);

    // "Add to file Open with menu" button - flat border, matching chrome color.
    {
        Fl_Color chromeCol = theme ? theme->colors().background2 : FL_BACKGROUND2_COLOR;
        m_chooseExtsBtn->color(chromeCol);
        m_chooseExtsBtn->selection_color(theme ? theme->colors().highlight1 : FL_SELECTION_COLOR);
        m_chooseExtsBtn->labelsize(uiFontSize);
        m_chooseExtsBtn->labelcolor(theme ? theme->colors().text1 : FL_BLACK);
    }

    // Choice boxes: rectangular flat border
    Fl_Color choiceBg = theme ? theme->colors().background1 : FL_WHITE;
    Fl_Color choiceFg = theme ? theme->colors().text1 : FL_BLACK;
    for (auto *ch : { m_tabWidthChoice, m_autoSaveChoice }) {
        ch->box(FL_BORDER_BOX);
        ch->down_box(FL_BORDER_BOX);
        ch->color(choiceBg);
        ch->textcolor(choiceFg);
        ch->labelsize(uiFontSize);
        ch->selection_color(theme ? theme->colors().highlight2 : FL_SELECTION_COLOR);
    }
    m_longLineInput->box(FL_BORDER_BOX);
    m_longLineInput->color(choiceBg);
    m_longLineInput->textcolor(choiceFg);
    m_longLineInput->cursor_color(choiceFg);   // caret matches the main editor
    m_longLineInput->labelsize(uiFontSize);
    m_longLineInput->selection_color(theme ? theme->colors().highlight2 : FL_SELECTION_COLOR);
    m_searchUrlInput->box(FL_BORDER_BOX);          // 与长行标记同款式
    m_searchUrlInput->color(choiceBg);
    m_searchUrlInput->textcolor(choiceFg);
    m_searchUrlInput->cursor_color(choiceFg);      // caret matches the main editor
    m_searchUrlInput->labelsize(uiFontSize);
    m_searchUrlInput->selection_color(theme ? theme->colors().highlight2 : FL_SELECTION_COLOR);

    // --- Bottom button bar (bgChrome like status bar) ---
    Fl_Color chromeCol = theme ? theme->colors().background2 : FL_BACKGROUND2_COLOR;
    Fl_Group *btnBar = new Fl_Group(0, btnBarY, w, gBarH);
    btnBar->box(FL_FLAT_BOX);
    btnBar->color(chromeCol);

    int btnY = btnBarY + (gBarH - btnH) / 2;
    const int gap = 8;
    m_okBtn = new HoverButton(0, btnY, 0, btnH, I18n::get("settings.ok"));
    m_okBtn->color(chromeCol);
    m_okBtn->selection_color(theme ? theme->colors().highlight1 : FL_SELECTION_COLOR);
    if (auto *hb = dynamic_cast<HoverButton *>(m_okBtn))
        hb->setPressColor(theme ? theme->colors().highlight2 : 0);
    m_okBtn->labelsize(uiFontSize);
    m_okBtn->labelcolor(theme ? theme->colors().text1 : FL_BLACK);
    m_okBtn->callback(cbOk, this);

    m_cancelBtn = new HoverButton(0, btnY, 0, btnH, I18n::get("settings.cancel"));
    m_cancelBtn->color(chromeCol);
    m_cancelBtn->selection_color(theme ? theme->colors().highlight1 : FL_SELECTION_COLOR);
    if (auto *hb = dynamic_cast<HoverButton *>(m_cancelBtn))
        hb->setPressColor(theme ? theme->colors().highlight2 : 0);
    m_cancelBtn->labelsize(uiFontSize);
    m_cancelBtn->labelcolor(theme ? theme->colors().text1 : FL_BLACK);
    m_cancelBtn->callback(cbCancel, this);

    // Auto-width right-aligned row: [OK] [Cancel].
    fitButtonRow({m_okBtn, m_cancelBtn}, w, btnBarY + gBarH / 2, margin, gap);

    btnBar->resizable(nullptr);
    btnBar->end();

    // Style the scrollbar: width, no-arrow type, and flat box to avoid
    // FLTK's default 3D border lines bleeding through the custom draw.
    scroll->scrollbar_size(10);  // match main window scrollbar width
    scroll->scrollbar.type(FL_VERT_SLIDER);  // no arrow buttons, like main window
    scroll->scrollbar.box(FL_FLAT_BOX);
    scroll->scrollbar.color(theme ? theme->colors().background1 : fl_rgb_color(170, 170, 170));
    scroll->scrollbar.selection_color(theme ? theme->colors().background2 : fl_rgb_color(235, 235, 235));

    // The shared title bar is created by DialogBase (initShell).
    end();
    finalizeShell();
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::loadFrom(const Config &cfg) {
    // Tab width: map the persisted value to one of {2, 4, 8}.
    // Anything else (e.g. edited manually in settings.ini) falls back
    // to 4, which is also the default for a fresh install.
    int tw = const_cast<Config &>(cfg).getTabWidth();
    int twIdx = 1;  // default = 4
    for (int i = 0; i < g_tabWidthCount; ++i) {
        if (g_tabWidths[i] == tw) { twIdx = i; break; }
    }
    m_tabWidthChoice->value(twIdx);

    m_autoIndentChk->value(const_cast<Config &>(cfg).getAutoIndent() ? 1 : 0);
    m_multiTabChk->value(const_cast<Config &>(cfg).getMultiTab() ? 1 : 0);
    m_trimTrailingChk->value(const_cast<Config &>(cfg).getTrimTrailingWhitespace() ? 1 : 0);
    m_trimLeadingChk->value(const_cast<Config &>(cfg).getTrimLeadingBlank() ? 1 : 0);
    m_trimEndingChk->value(const_cast<Config &>(cfg).getTrimEndingBlank() ? 1 : 0);
    m_expandTabsChk->value(const_cast<Config &>(cfg).getExpandTabsOnSave() ? 1 : 0);

    // Auto-save interval: map 0,30,60,120,300 → index 0..4
    int as = const_cast<Config &>(cfg).getAutoSaveInterval();
    static const int asVals[] = { 0, 30, 60, 120, 300 };
    int asIdx = 0;
    for (int i = 0; i < 5; ++i) { if (asVals[i] == as) { asIdx = i; break; } }
    m_autoSaveChoice->value(asIdx);

    // System integration: read current registry state.
    // The "New > Text Document" entry is enabled if .txt\ShellNew\NullFile
    // exists. The "Open with" master switch is enabled if at least one
    // extension (preset OR custom from the INI) has a Pecia entry under
    // OpenWithProgids.
    m_integNewTxt->value(regKeyExists(HKEY_CURRENT_USER, L"Software\\Classes\\.txt\\ShellNew", L"NullFile") ? 1 : 0);

    // Load the persisted extension selection. If the INI key is missing
    // (first run or upgraded from an older version) we default to ALL
    // preset extensions checked so the user sees the full list on first
    // use and can uncheck what they don't need.
    {
        char buf[1024];
        const_cast<Config &>(cfg).getOpenWithExts(buf, sizeof(buf), "");
        std::wstring ws;
        for (const char *p = buf; *p; ++p) ws.push_back((wchar_t)(unsigned char)*p);
        if (ws.empty()) {
            for (int i = 0; i < kAssocExtCount; ++i) {
                if (i) ws += L",";
                ws += kAssocExts[i];
            }
        }
        m_openWithExts = splitExts(ws);
    }

    m_detectUrlsChk->value(const_cast<Config &>(cfg).getDetectUrls() ? 1 : 0);
    m_cleanupTempChk->value(const_cast<Config &>(cfg).getCleanupTempOld() ? 1 : 0);

    char urlBuf[512];
    const_cast<Config &>(cfg).getSearchEngineUrl(urlBuf, sizeof(urlBuf));
    m_searchUrlInput->value(urlBuf);

    char llBuf[16];
    snprintf(llBuf, sizeof(llBuf), "%d", const_cast<Config &>(cfg).getLongLineMarker());
    m_longLineInput->value(llBuf);
}

bool SettingsDialog::saveTo(Config &cfg) const {
    bool changed = false;

    // Tab width: 2 / 4 / 8 only. We always write back the chosen value
    // (even if it equals the previous one) so a stale INI entry like
    // tab_width=6 gets normalized to 4 on the next save.
    int twIdx = m_tabWidthChoice->value();
    int tw = (twIdx >= 0 && twIdx < g_tabWidthCount) ? g_tabWidths[twIdx] : 4;
    if (cfg.getTabWidth() != tw) {
        cfg.setTabWidth(tw);
        changed = true;
    }

    bool ai = m_autoIndentChk->value() != 0;
    if (cfg.getAutoIndent() != ai) {
        cfg.setAutoIndent(ai);
        changed = true;
    }

    bool mt = m_multiTabChk->value() != 0;
    if (cfg.getMultiTab() != mt) {
        // The actual UI switch (detaching tabs when turning OFF, or
        // switching layout when turning ON) is handled by the caller in
        // applySettings() - we only persist the choice here. If the
        // caller's detach fails (user cancels a Save As prompt), it
        // will revert this entry.
        cfg.setMultiTab(mt);
        changed = true;
    }

    bool tt = m_trimTrailingChk->value() != 0;
    if (cfg.getTrimTrailingWhitespace() != tt) {
        cfg.setTrimTrailingWhitespace(tt);
        changed = true;
    }

    bool tl = m_trimLeadingChk->value() != 0;
    if (cfg.getTrimLeadingBlank() != tl) {
        cfg.setTrimLeadingBlank(tl);
        changed = true;
    }

    bool te = m_trimEndingChk->value() != 0;
    if (cfg.getTrimEndingBlank() != te) {
        cfg.setTrimEndingBlank(te);
        changed = true;
    }

    bool ex = m_expandTabsChk->value() != 0;
    if (cfg.getExpandTabsOnSave() != ex) {
        cfg.setExpandTabsOnSave(ex);
        changed = true;
    }

    // Auto-save interval
    static const int asVals[] = { 0, 30, 60, 120, 300 };
    int asIdx = m_autoSaveChoice->value();
    int as = (asIdx >= 0 && asIdx < 5) ? asVals[asIdx] : 0;
    if (cfg.getAutoSaveInterval() != as) {
        cfg.setAutoSaveInterval(as);
        changed = true;
    }

    // System integration: apply "New > Text Document" registry changes
    // and persist the extension list. The "Open with" registry entries
    // are applied immediately when the ExtensionsDialog is confirmed
    // (see cbChooseExts), not here - this keeps the add-only semantics
    // (unchecking an extension in the dialog leaves its registry entries
    // alone, it just won't be re-added on future OKs).
    bool newTxt = m_integNewTxt->value() != 0;
    applyNewTxtRegistry(newTxt);
    {
        // Persist the extension list as a UTF-8 string. All supported
        // extensions are ASCII, so a simple narrowing copy is safe.
        std::wstring extsStr = joinExts(m_openWithExts);
        std::string utf8;
        utf8.reserve(extsStr.size());
        for (wchar_t wc : extsStr) utf8.push_back((char)wc);
        cfg.setOpenWithExts(utf8.c_str());
    }
    // Always mark changed so the caller refreshes things even if only
    // the registry was updated (the INI value itself may be unchanged).
    changed = true;

    bool du = m_detectUrlsChk->value() != 0;
    if (cfg.getDetectUrls() != du) {
        cfg.setDetectUrls(du);
        changed = true;
    }

    bool ct = m_cleanupTempChk->value() != 0;
    if (cfg.getCleanupTempOld() != ct) {
        cfg.setCleanupTempOld(ct);
        changed = true;
    }

    // 搜索引擎 URL 模板（%s = 选中内容）
    const char *su = m_searchUrlInput->value();
    if (su && *su) {
        char cur[512];
        cfg.getSearchEngineUrl(cur, sizeof(cur));
        if (strcmp(cur, su) != 0) {
            cfg.setSearchEngineUrl(su);
            changed = true;
        }
    }

    int ll = atoi(m_longLineInput->value());
    if (ll < 0) ll = 0;
    if (cfg.getLongLineMarker() != ll) {
        cfg.setLongLineMarker(ll);
        changed = true;
    }

    return changed;
}

int SettingsDialog::handle(int event) {
    // User-assignable OK shortcut (Window > Button > OK): trigger the
    // OK button before any focused input widget eats the key.
    if (event == FL_KEYDOWN || event == FL_SHORTCUT) {
        if (dialogOkShortcutMatches(Fl::event_key(), Fl::event_state())) {
            cbOk(nullptr, this);
            return 1;
        }
    }
    return DialogBase::handle(event);
}

bool SettingsDialog::runModal() {
    // Center on screen (mirrors InfoWindow::show()).
    position((Fl::w() - w()) / 2, (Fl::h() - h()) / 2);

    m_okBtn->when(FL_WHEN_RELEASE);
    m_cancelBtn->when(FL_WHEN_RELEASE);
    // user_data holds the "accepted" flag (0 = cancelled). Reset it so
    // closing via the title bar X counts as cancel.
    user_data(reinterpret_cast<void *>((intptr_t)0));
    show();

#if defined(_WIN32)
    HWND hwnd = (HWND)fl_xid(this);
    // Make sure the dialog shows up on the taskbar like a real window.
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                      GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_APPWINDOW);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);

#endif

    while (shown()) Fl::wait();
    // After the loop exits, we check whether the OK button was clicked
    // by looking at user_data (stashed by cbOk / cbCancel). The title
    // bar close button just calls hide() without touching user_data, so
    // it correctly counts as cancel (user_data stays 0).
    return reinterpret_cast<intptr_t>(user_data()) != 0;
}

void SettingsDialog::cbOk(Fl_Widget * /*w*/, void *data) {
    SettingsDialog *self = static_cast<SettingsDialog *>(data);
    if (!self) return;
    // Stash a "true" flag so runModal() knows we accepted.
    self->user_data(reinterpret_cast<void *>((intptr_t)1));
    self->hide();
}

void SettingsDialog::cbCancel(Fl_Widget * /*w*/, void *data) {
    SettingsDialog *self = static_cast<SettingsDialog *>(data);
    if (!self) return;
    self->user_data(reinterpret_cast<void *>((intptr_t)0));
    self->hide();
}

// ---------------------------------------------------------------------------
// ExtensionsDialog - secondary modal dialog for choosing which file
// extensions Pecia registers itself for in the "Open with" menu.
// Listed as a single column of checkboxes with "Select All" / "None"
// shortcuts. A DialogBase modal dialog (shared title bar / border).
// ---------------------------------------------------------------------------
namespace {

struct ExtensionsDialog : DialogBase {
    std::vector<Fl_Check_Button*> m_checks;
    Fl_Input *m_customInput = nullptr;
    Fl_Box   *m_customHint = nullptr;   // grey example hint inside custom input
    HoverButton  *m_allBtn = nullptr;
    HoverButton  *m_okBtn = nullptr;   // for re-positioning allBtn on toggle
    const Theme *m_theme = nullptr;

    ExtensionsDialog(int W, int H, const char *title, const Theme *theme, int uiFontSize)
        : DialogBase(W, H + TITLE_H, title, theme, uiFontSize, ModalDialog), m_theme(theme) {
        begin();
        initShell(title);

        int margin = 12;
        int rowH = 22;
        int btnH = 22;
        int btnY = H;  // bottom bar y (window coords, before +TITLE_H)

        // Layout: scrollable checkbox area on top, custom input row
        // immediately below it, bottom button bar at the very bottom.
        // The custom input row height matches rowH for visual consistency
        // with the checkboxes.
        int customRowH = rowH + 8;  // a bit of breathing room above the input
        int customY = btnY - customRowH;
        int scrollH = customY - TITLE_H;

        Fl_Scroll *scroll = new Fl_Scroll(0, TITLE_H, W, scrollH);
        scroll->box(FL_FLAT_BOX);
        scroll->color(theme ? theme->colors().background1 : FL_WHITE);
        scroll->scrollbar_size(10);
        scroll->scrollbar.type(FL_VERT_SLIDER);
        scroll->scrollbar.box(FL_FLAT_BOX);
        scroll->scrollbar.color(theme ? theme->colors().background1 : fl_rgb_color(170,170,170));
        scroll->scrollbar.selection_color(theme ? theme->colors().background2 : fl_rgb_color(235,235,235));

        // Two-column layout: split kAssocExts into two halves. The first
        // half fills the left column top-to-bottom, the second half fills
        // the right column. This keeps related extensions (plain text,
        // config, code, ...) visually grouped.
        const int cols = 2;
        int perCol = (kAssocExtCount + cols - 1) / cols;  // ceil(22/2) = 11
        int colW = (W - margin * 2) / cols;

        fl_font(FL_HELVETICA, uiFontSize);
        for (int i = 0; i < kAssocExtCount; ++i) {
            int col = i / perCol;
            int row = i % perCol;
            int chkX = margin + col * colW;
            int y = TITLE_H + row * rowH;
            int chkW = colW - 8;

            // All extensions are ASCII - simple narrowing copy is safe.
            // copy_label() avoids the dangling-pointer trap of the
            // Fl_Check_Button constructor (which only stores the pointer).
            char label[16];
            int n = 0;
            for (const wchar_t *p = kAssocExts[i]; *p && n < 15; ++p)
                label[n++] = (char)*p;
            label[n] = 0;

            auto *cb = new Fl_Check_Button(chkX, y, chkW, rowH, "");
            cb->copy_label(label);
            cb->box(FL_NO_BOX);
            cb->down_box(FL_BORDER_BOX);
            cb->color(theme ? theme->colors().background1 : FL_WHITE);
            cb->selection_color(theme ? theme->colors().highlight2 : FL_SELECTION_COLOR);
            cb->labelsize(uiFontSize);
            cb->labelcolor(theme ? theme->colors().text1 : FL_BLACK);
            m_checks.push_back(cb);
        }
        scroll->end();

        // Custom extension input row. Lets the user add extensions that
        // aren't in the preset list (e.g. ".py .go .rs"). Entries are
        // space-separated; leading dots are optional.
        Fl_Color editorCol = theme ? theme->colors().background1 : FL_WHITE;
        Fl_Color textCol = theme ? theme->colors().text1 : FL_BLACK;
        Fl_Group *customBar = new Fl_Group(0, customY, W, customRowH);
        customBar->box(FL_FLAT_BOX);
        customBar->color(editorCol);
        fl_font(FL_HELVETICA, uiFontSize);
        const char *kCustomLbl = I18n::get("settings.custom");
        int lblW = (int)(fl_width(kCustomLbl) + 0.5) + 12;  // text + 12px padding
        int inputX = margin + lblW;
        int inputW = W - margin - inputX;
        int inputY = customY + (customRowH - btnH) / 2;
        Fl_Box *customLbl = new Fl_Box(margin, inputY, lblW, btnH, kCustomLbl);
        customLbl->box(FL_NO_BOX);
        customLbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        customLbl->labelsize(uiFontSize);
        customLbl->labelcolor(textCol);
        m_customInput = new Fl_Input(inputX, inputY, inputW, btnH, "");
        m_customInput->box(FL_BORDER_BOX);
        m_customInput->color(editorCol);
        m_customInput->textcolor(textCol);
        m_customInput->textsize(uiFontSize);
        m_customInput->labelsize(uiFontSize);
        // Grey example hint inside the input (like the find bar's "Find:"
        // note): tells the user how to list multiple suffixes, using the
        // secondary-text colour. Shown only while the input is empty.
        m_customHint = new Fl_Box(inputX + 4, inputY,
                                  inputW - 6, btnH,
                                  I18n::get("settings.customhint"));
        m_customHint->box(FL_NO_BOX);
        m_customHint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        m_customHint->labelsize(uiFontSize);
        m_customHint->labelcolor(theme ? theme->colors().text2 : fl_rgb_color(140, 140, 140));
        m_customHint->hide();
        m_customInput->callback([](Fl_Widget * /*w*/, void *d) {
            ExtensionsDialog *self = static_cast<ExtensionsDialog *>(d);
            if (!self || !self->m_customHint) return;
            if (self->m_customInput->value()[0])
                self->m_customHint->hide();
            else
                self->m_customHint->show();
            self->m_customInput->redraw();
        }, this);
        m_customInput->when(FL_WHEN_CHANGED);
        customBar->end();

        // Bottom bar: Select All (toggle) then OK, right-aligned to the
        // dialog margin (buttons elsewhere in Pecia sit on the right, e.g.
        // the AI dialog). Each button's width is sized to its label (plus
        // padding) so they don't waste horizontal space; the toggle keeps
        // the wider of its two labels so it never jumps while switching.
        Fl_Color chromeCol = theme ? theme->colors().background2 : FL_BACKGROUND2_COLOR;
        Fl_Group *btnBar = new Fl_Group(0, btnY, W, TITLE_H);
        btnBar->box(FL_FLAT_BOX);
        btnBar->color(chromeCol);

        const char *kAllLbl   = I18n::get("settings.selectall");
        int gap  = 8;
        int okY  = btnY + (TITLE_H - btnH) / 2;
        // created with zero width; fitButtonRow sizes + right-aligns below.
        HoverButton *allBtn = new HoverButton(0, okY, 0, btnH, kAllLbl);
        HoverButton *okBtn  = new HoverButton(0, okY, 0, btnH, I18n::get("settings.ok"));
        m_allBtn = allBtn;
        m_okBtn = okBtn;
        for (auto *b : { allBtn, okBtn }) {
            b->color(chromeCol);
            b->selection_color(theme ? theme->colors().highlight1 : FL_SELECTION_COLOR);
            b->setPressColor(theme ? theme->colors().highlight2 : 0);
            b->labelsize(uiFontSize);
            b->labelcolor(theme ? theme->colors().text1 : FL_BLACK);
        }
        // Select-All button starts at its current label width; refreshToggleLabel
        // re-fits it on every toggle (so it always matches the text shown).
        allBtn->copy_label(kAllLbl);
        allBtn->fit();
        okBtn->fit();
        int okW = okBtn->w();
        int okX  = W - margin - okW;
        allBtn->position(okX - gap - allBtn->w(), okY);
        okBtn->position(okX, okY);
        allBtn->callback([](Fl_Widget*, void *data){
            auto *self = static_cast<ExtensionsDialog*>(data);
            // Toggle: everything selected -> clear all, otherwise select all.
            bool allSel = !self->m_checks.empty();
            for (auto *cb : self->m_checks)
                if (!cb->value()) { allSel = false; break; }
            for (auto *cb : self->m_checks) allSel ? cb->clear() : cb->set();
            self->refreshToggleLabel();
        }, this);
        okBtn->callback([](Fl_Widget*, void *data){
            auto *self = static_cast<ExtensionsDialog*>(data);
            self->user_data(reinterpret_cast<void*>((intptr_t)1));
            self->hide();
        }, this);
        btnBar->end();

        // Title bar (created last so it sits on top).
        // The shared title bar is created by DialogBase.
        end();
        finalizeShell();
    }

    // Update the Select All button label to match the current state:
    // all checked -> "Deselect All", otherwise "Select All".
    void refreshToggleLabel() {
        if (!m_allBtn) return;
        bool allSel = !m_checks.empty();
        for (auto *cb : m_checks)
            if (!cb->value()) { allSel = false; break; }
        m_allBtn->copy_label(I18n::get(allSel ? "settings.deselectall" : "settings.selectall"));
        // Re-fit the width to the current label and re-anchor it to the OK
        // button so it never overflows the hover box when the text changes.
        if (m_okBtn) {
            m_allBtn->fit();
            int gap = 8;
            m_allBtn->position(m_okBtn->x() - gap - m_allBtn->w(), m_allBtn->y());
        }
        m_allBtn->redraw();
    }
};

// Parse the user's "Custom:" input into a list of normalized extensions.
// Rules:
//  - Whitespace and commas are both treated as separators.
//  - A leading dot is added if missing ("py" -> ".py").
//  - ASCII letters are lowercased so ".PY" and ".py" compare equal.
//  - Entries with non-alphanumeric characters (e.g. ".c++") are skipped.
//  - Duplicates (after normalization) are removed.
static std::vector<std::wstring> parseCustomExts(const char *text) {
    std::vector<std::wstring> out;
    std::string s(text ? text : "");
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && !isalnum((unsigned char)s[i])) ++i;
        size_t start = i;
        while (i < s.size() && isalnum((unsigned char)s[i])) ++i;
        if (i > start) {
            std::string tok = s.substr(start, i - start);
            for (auto &c : tok) c = (char)tolower((unsigned char)c);
            // Reconstruct as ".ext" (with leading dot).
            std::wstring w = L".";
            for (char c : tok) w += (wchar_t)(unsigned char)c;
            // Dedup.
            bool dup = false;
            for (const auto &e : out) if (e == w) { dup = true; break; }
            if (!dup) out.push_back(w);
        }
    }
    return out;
}

} // namespace

void SettingsDialog::openExtensionsForTest() {
    // Reuse the real ExtensionsDialog construction path (width/height/rows
    // identical to cbChooseExts) but display-only: no registry changes.
    const int dlgW = 400;
    const int rowH = 22;
    const int cols = 2;
    int perCol = (kAssocExtCount + cols - 1) / cols;
    int customRowH = rowH + 8;
    const int dlgH = perCol * rowH + customRowH + TITLE_H + 12;
    // A lightweight SettingsDialog owns the theme/fonts the extensions
    // dialog needs; `this` is already such an instance in the normal flow,
    // but for the Test Dialogs entry we may not have one, so use this.
    ExtensionsDialog dlg(dlgW, dlgH, I18n::get("settings.filetype"),
                         m_theme, m_uiFontSize);
    dlg.position((Fl::w() - dlgW) / 2, (Fl::h() - dlgH - TITLE_H) / 2);
    dlg.refreshToggleLabel();
    dlg.user_data(reinterpret_cast<void *>((intptr_t)0));
    dlg.show();
    if (ui::g_smokeMode) { dlg.hide(); return; }
    while (dlg.shown()) Fl::wait();
}

void SettingsDialog::cbChooseExts(Fl_Widget * /*w*/, void *data) {
    SettingsDialog *self = static_cast<SettingsDialog *>(data);
    if (!self) return;

    // Dialog width: wide enough for two columns of checkboxes plus the
    // three auto-sized bottom buttons. Height fits 11 rows (one column's
    // worth for the 22-extension preset list) plus the custom input row
    // and the title bar, with a small margin so no scrollbar appears.
    const int dlgW = 400;
    const int rowH = 22;
    const int cols = 2;
    int perCol = (kAssocExtCount + cols - 1) / cols;
    int customRowH = rowH + 8;
    const int dlgH = perCol * rowH + customRowH + TITLE_H + 12;
    ExtensionsDialog dlg(dlgW, dlgH, I18n::get("settings.filetype"), self->m_theme, self->m_uiFontSize);
    dlg.position((Fl::w() - dlgW) / 2, (Fl::h() - dlgH - TITLE_H) / 2);

    // Initialize checkboxes from the current selection. Track which selected
    // extensions are NOT in the preset list - those go into the Custom box.
    std::vector<std::wstring> customExts;
    for (int i = 0; i < kAssocExtCount; ++i) {
        bool sel = false;
        for (const auto &e : self->m_openWithExts) {
            if (e == kAssocExts[i]) { sel = true; break; }
        }
        dlg.m_checks[i]->value(sel ? 1 : 0);
    }
    dlg.refreshToggleLabel();
    for (const auto &e : self->m_openWithExts) {
        bool inPreset = false;
        for (int i = 0; i < kAssocExtCount; ++i) {
            if (e == kAssocExts[i]) { inPreset = true; break; }
        }
        if (!inPreset) customExts.push_back(e);
    }
    // Render the custom list as a space-separated string for the input.
    {
        std::string customStr;
        for (size_t i = 0; i < customExts.size(); ++i) {
            if (i) customStr += " ";
            for (wchar_t wc : customExts[i]) {
                if (wc < 128) customStr += (char)wc;
            }
        }
        dlg.m_customInput->value(customStr.c_str());
        // Sync the grey example hint: visible only when the box is empty.
        if (dlg.m_customHint) {
            if (customStr.empty()) dlg.m_customHint->show();
            else                  dlg.m_customHint->hide();
        }
    }

    dlg.user_data(reinterpret_cast<void *>((intptr_t)0));
    dlg.show();
    while (dlg.shown()) Fl::wait();

    if (reinterpret_cast<intptr_t>(dlg.user_data()) != 1) return;  // cancelled

    // Read back the selection: preset checkboxes + parsed custom input.
    self->m_openWithExts.clear();
    for (int i = 0; i < kAssocExtCount; ++i) {
        if (dlg.m_checks[i]->value()) {
            self->m_openWithExts.push_back(kAssocExts[i]);
        }
    }
    auto parsed = parseCustomExts(dlg.m_customInput->value());
    for (const auto &e : parsed) {
        // Avoid duplicating an entry that's also checked in the preset.
        bool dup = false;
        for (const auto &existing : self->m_openWithExts) {
            if (existing == e) { dup = true; break; }
        }
        if (!dup) self->m_openWithExts.push_back(e);
    }

    // Immediately register the selected extensions in the registry.
    // This is an add-only operation - unchecked extensions are left
    // as-is in the registry (Windows protects OpenWithList from cleanup).
    applyOpenWithRegistry(joinExts(self->m_openWithExts));
}
