// Config.cpp - INI-backed persistent settings (cross-platform)
#include "Config.h"
#include "PathUtils.h"

#include <FL/Fl.H>              // Fl::set_fonts / Fl::get_font_name
#include <FL/Enumerations.H>   // FL_COURIER
#include <FL/filename.H>       // FL_PATH_MAX
#include <FL/fl_string_functions.h>  // fl_strlcpy
#include <FL/fl_utf8.h>        // fl_fopen, fl_unlink (UTF-8 aware)

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>

#include <functional>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

static const char *SECTION = "Settings";
static const int MAX_RECENT = 10;

namespace fs = std::filesystem;

// Forward declaration for Fl::awake callback
static void changeCallbackHandler(void *data);

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

fs::path Config::getExeDir() const {
    // NOTE: must use pathutil::exeDirPath(), not fs::path(exeDir()).
    // fs::path(const std::string&) decodes bytes as the ANSI codepage, so a
    // UTF-8 exeDir() would be mojibake'd on Chinese paths (see PathUtils.h).
    return pathutil::exeDirPath();
}

void Config::refreshFileTime() {
    std::error_code ec;
    auto t = fs::last_write_time(m_iniPath, ec);
    if (ec) {
        m_lastWriteTime = fs::file_time_type{};
    } else {
        m_lastWriteTime = t;
    }
}

// ---------------------------------------------------------------------------
// INI load / save (platform-agnostic, no Win32 dependency)
// ---------------------------------------------------------------------------

void Config::loadAll(std::vector<std::pair<std::string, std::string>> &entries) {
    entries.clear();
    std::ifstream in(m_iniPath);
    if (!in.is_open()) return;

    bool inSection = false;
    std::string line;
    while (std::getline(in, line)) {
        // Strip trailing \r (in case of CRLF line endings on Windows)
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Skip blank lines and comments
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        // Section header
        if (line.front() == '[' && line.back() == ']') {
            inSection = (line == "[" + std::string(SECTION) + "]");
            continue;
        }

        if (!inSection) continue;

        // key=value
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        // Trim leading/trailing whitespace from key
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        size_t ks = 0;
        while (ks < key.size() && (key[ks] == ' ' || key[ks] == '\t')) ++ks;
        key = key.substr(ks);

        // Trim leading/trailing whitespace from value too (hand-edited
        // INIs often carry a stray space/CR after '=', which would
        // otherwise become part of e.g. the API key).
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
            value.pop_back();
        size_t vs = 0;
        while (vs < value.size() && (value[vs] == ' ' || value[vs] == '\t')) ++vs;
        value = value.substr(vs);

        if (!key.empty()) {
            entries.emplace_back(std::move(key), std::move(value));
        }
    }
}

// Chinese one-line description of a settings.ini key, written as a "# "
// comment line directly above the key when saveAll() writes the file, so a
// user opening settings.ini can see what each parameter does. Unknown keys
// return nullptr (no comment). The parser already ignores '#' lines.
static const char *configKeyDesc(const char *key) {
    if (!key) return nullptr;
    if (strcmp(key,"ai_api_key")==0) return "大模型 API 密钥（留空不发请求）";
    if (strcmp(key,"ai_endpoint")==0) return "大模型接口地址";
    if (strcmp(key,"ai_model")==0) return "大模型名称";
    if (strcmp(key,"always_on_top")==0) return "窗口是否总是置顶 (1=是 0=否)";
    if (strcmp(key,"auto_indent")==0) return "是否自动缩进 (1=是 0=否)";
    if (strcmp(key,"auto_save_interval")==0) return "自动保存间隔（秒）";
    if (strcmp(key,"bar_height")==0) return "标题栏/工具栏高度（像素）";
    if (strcmp(key,"btn_height")==0) return "按钮高度（像素）";
    if (strcmp(key,"cleanup_temp_old")==0) return "退出时清理 7 天前的临时文件 (1=是 0=否)";
    if (strcmp(key,"detect_urls")==0) return "是否自动识别并高亮网址 (1=是 0=否)";
    if (strcmp(key,"dialog_pad")==0) return "对话框内容边距（像素）";
    if (strcmp(key,"editor_font")==0) return "编辑器字体名";
    if (strcmp(key,"editor_zoom_size")==0) return "编辑区缩放字号（0=未缩放，跟随界面字号）";
    if (strcmp(key,"expand_tabs_on_save")==0) return "保存时是否把 Tab 转为空格 (1=是 0=否)";
    if (strcmp(key,"fixed_startup_doc")==0) return "无其它 Pecia 进程时，启动直接打开 exe 同级目录的 Test.txt (1=是 0=否)";
    if (strcmp(key,"highlight_current_line")==0) return "是否高亮当前行 (1=是 0=否)";
    if (strcmp(key,"lang")==0) return "界面语言 (en / zh-CN)";
    if (strcmp(key,"last_dir")==0) return "上次打开/保存文件的目录";
    if (strcmp(key,"line_numbers")==0) return "是否显示行号 (1=是 0=否)";
    if (strcmp(key,"long_line_marker")==0) return "长行标记列数 (0=关闭)";
    if (strcmp(key,"multi_tab")==0) return "是否启用多标签页 (1=是 0=否)";
    if (strcmp(key,"open_with_exts")==0) return "加入右键“打开方式”的扩展名（逗号分隔）";
    if (strcmp(key,"preview_auto_refresh")==0) return "Markdown 预览自动刷新间隔（毫秒，0=关闭）";
    if (strcmp(key,"preview_scroll_sync")==0) return "预览与编辑滚动是否同步 (1=是 0=否)";
    if (strcmp(key,"scheme")==0) return "FLTK 界面风格 (gtk+)";
    if (strcmp(key,"search_engine_url")==0) return "搜索引擎 URL 模板（%s 为选中内容）";
    if (strcmp(key,"show_statusbar")==0) return "是否显示状态栏 (1=是 0=否)";
    if (strcmp(key,"show_toolbar")==0) return "是否显示脚本/工具栏 (1=是 0=否)";
    if (strcmp(key,"show_whitespace")==0) return "是否显示空格符号 (1=是 0=否)";
    if (strcmp(key,"tab_width")==0) return "Tab 宽度（空格数）";
    if (strcmp(key,"text_bar_height")==0) return "文字栏高度（像素）";
    if (strcmp(key,"theme.name")==0) return "当前主题名（颜色见 theme/ 文件夹，如 light/dark/green）";
    if (strcmp(key,"trim_ending_blank")==0) return "保存时是否删除行尾多余空行 (1=是 0=否)";
    if (strcmp(key,"trim_leading_blank")==0) return "保存时是否删除行首空行 (1=是 0=否)";
    if (strcmp(key,"trim_trailing_whitespace")==0) return "保存时是否删除行尾空白 (1=是 0=否)";
    if (strcmp(key,"ui_font_size")==0) return "界面字号";
    if (strcmp(key,"win_x")==0) return "主窗口 X 坐标";
    if (strcmp(key,"win_y")==0) return "主窗口 Y 坐标";
    if (strcmp(key,"win_w")==0) return "主窗口宽度";
    if (strcmp(key,"win_h")==0) return "主窗口高度";
    if (strcmp(key,"win_flags")==0) return "主窗口标志位";
    if (strcmp(key,"main_win_w")==0) return "主窗口宽度";
    if (strcmp(key,"main_win_h")==0) return "主窗口高度";
    if (strcmp(key,"main_win_x")==0) return "主窗口 X 坐标（上次退出时的位置）";
    if (strcmp(key,"main_win_y")==0) return "主窗口 Y 坐标（上次退出时的位置）";
    if (strcmp(key,"main_win_max")==0) return "上次退出时是否最大化 (1=是 0=否)";
    if (strcmp(key,"lua_win_w")==0) return "Lua 控制台窗口宽度";
    if (strcmp(key,"lua_win_h")==0) return "Lua 控制台窗口高度";
    if (strcmp(key,"aichat_win_w")==0) return "AI 聊天窗口宽度";
    if (strcmp(key,"aichat_win_h")==0) return "AI 聊天窗口高度";
    if (strcmp(key,"wrap")==0) return "是否自动换行 (1=是 0=否)";
    return nullptr;
}

void Config::saveAll() {
    // Make sure the parent directory exists. On a normal install the
    // exe lives in its own directory which is already there, but for
    // portability (and to support things like running from a build
    // folder that was just created) we create it if missing.
    std::error_code ec;
    fs::create_directories(m_iniPath.parent_path(), ec);
    ec.clear();

    // Merge strategy: settings.ini is shared by several processes. Only
    // keys this process actually modified (m_dirtyKeys) may overwrite
    // the on-disk value; every other key keeps the current file content.
    // This prevents a stale in-memory snapshot from clobbering settings
    // another process just wrote (e.g. the AI window must never reset
    // the UI language back to English).
    const bool fileExists = fs::exists(m_iniPath);
    std::map<std::string, std::string> merged;
    if (fileExists) {
        std::vector<std::pair<std::string, std::string>> disk;
        loadAll(disk);
        for (auto &kv : disk) merged[kv.first] = kv.second;
    }
    for (auto &kv : m_entries) {
        // Write whitelist (AI process): only touch the allowed keys.
        // Ignored when the file does not exist so a full default file
        // is generated from scratch.
        if (!m_writeWhitelist.empty() && fileExists &&
            std::find(m_writeWhitelist.begin(), m_writeWhitelist.end(),
                      kv.first) == m_writeWhitelist.end()) {
            continue;
        }
        const bool dirty = m_dirtyKeys.count(kv.first) != 0;
        if (dirty) merged[kv.first] = kv.second;
        else if (merged.find(kv.first) == merged.end())
            merged[kv.first] = kv.second;   // new key (default fill)
    }
    m_dirtyKeys.clear();

    // Atomic replace: write to a temp file first, then rename over the
    // real path. Other processes watch this file with
    // FindFirstChangeNotification, which fires the moment the first byte
    // is written - without the rename they would reload a half-written
    // file and lose keys (e.g. lang, reverting the UI to English).
    // The temp name is unique per save (pid + sequence): the old fixed
    // ".tmp" name let concurrent processes clobber each other's temp file
    // mid-write.
    static std::atomic<unsigned long> s_saveSeq{0};
    unsigned long seq = ++s_saveSeq;
    unsigned long pid = 0;
#if defined(_WIN32)
    pid = (unsigned long)GetCurrentProcessId();
#endif
    fs::path tmpPath = m_iniPath;
    tmpPath += ".tmp." + std::to_string(pid) + "." + std::to_string(seq);
    std::ofstream out(tmpPath, std::ios::trunc);
    if (!out.is_open()) return;
    out << "[" << SECTION << "]\n";
    for (const auto &kv : merged) {
        if (const char *desc = configKeyDesc(kv.first.c_str()))
            out << "# " << desc << "\n";
        out << kv.first << "=" << kv.second << "\n";
    }
    out.flush();
    bool writeOk = out.good();
    out.close();
    if (!writeOk) {
        // Write failed (disk full / antivirus lock): do NOT overwrite
        // settings.ini with a truncated/empty temp file, which would
        // silently destroy all settings (incl. the API key).
        std::error_code rmEc;
        fs::remove(tmpPath, rmEc);
        return;
    }

    bool replaced = false;
#if defined(_WIN32)
    if (MoveFileExW(tmpPath.wstring().c_str(), m_iniPath.wstring().c_str(),
                    MOVEFILE_REPLACE_EXISTING))
        replaced = true;
#else
    std::error_code ec;
    fs::rename(tmpPath, m_iniPath, ec);
    if (!ec) replaced = true;
#endif
    if (!replaced) {
        // Fall back to a direct write if the rename fails (e.g. another
        // process holds the file open without FILE_SHARE_WRITE).
        std::ofstream direct(m_iniPath, std::ios::trunc);
        if (direct.is_open()) {
            direct << "[" << SECTION << "]\n";
            for (const auto &kv : merged) {
                if (const char *desc = configKeyDesc(kv.first.c_str()))
                    direct << "# " << desc << "\n";
                direct << kv.first << "=" << kv.second << "\n";
            }
            direct.flush();
            direct.close();
        }
        std::error_code ec2;
        fs::remove(tmpPath, ec2);
    }

    refreshFileTime();
}

void Config::setWriteWhitelist(std::initializer_list<const char *> keys) {
    m_writeWhitelist.clear();
    for (const char *k : keys) m_writeWhitelist.push_back(k ? k : "");
}

void Config::reloadFromDisk() {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    std::vector<std::pair<std::string, std::string>> disk;
    loadAll(disk);
    for (auto &kv : disk) {
        // Keep values modified in this session; they will be flushed by
        // the next saveAll().
        if (!m_dirtyKeys.count(kv.first))
            m_entries[kv.first] = kv.second;
    }
}

// ---------------------------------------------------------------------------
// Read / write helpers
//
// All settings are cached in an in-memory std::map (m_entries). Each
// getter does a single map lookup; each setter updates the map and
// rewrites the entire INI file. The full rewrite is fast (a few dozen
// lines) and avoids the partial-write race that bit-by-bit updates
// would have across multiple processes / windows.
// ---------------------------------------------------------------------------

int Config::readInt(const char *key, int fallback) const {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    auto it = m_entries.find(key);
    if (it == m_entries.end()) return fallback;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return fallback;
    }
}

void Config::readStr(const char *key, char *buf, int len, const char *fallback) const {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        fl_strlcpy(buf, fallback ? fallback : "", len);
        return;
    }
    fl_strlcpy(buf, it->second.c_str(), len);
}

void Config::writeInt(const char *key, int value) {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    m_entries[key] = buf;
    m_dirtyKeys.insert(key);
    saveAll();
}

void Config::writeStr(const char *key, const char *value) {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    m_entries[key] = value ? value : "";
    m_dirtyKeys.insert(key);
    saveAll();
}

bool Config::validateKey(const char *key, int fallback, int minVal, int maxVal) {
    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        // Missing key — insert default.
        m_entries[key] = std::to_string(fallback);
        m_dirtyKeys.insert(key);
        return true;
    }
    if (minVal == 0 && maxVal == 0) return false;  // string key, no range check
    // Parse and validate.
    int val = 0;
    try { val = std::stoi(it->second); }
    catch (...) { val = fallback - 1; }  // force repair on parse failure
    if (val < minVal || val > maxVal) {
        m_entries[key] = std::to_string(fallback);
        m_dirtyKeys.insert(key);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Config::Config() {
    m_iniPath = getExeDir() / "settings.ini";
    m_recentFilePath = getExeDir() / "recent file.ini";
    initFromDisk();
}

// Test hook: point the config at a scratch directory so unit tests never
// touch the real settings.ini next to Pecia.exe.
Config::Config(const fs::path &configDir) {
    m_iniPath = configDir / "settings.ini";
    m_recentFilePath = configDir / "recent file.ini";
    initFromDisk();
}

// Standalone tools: own ini file (e.g. "PeciaLua.ini") so the main
// process's settings.ini is never written by another process.
Config::Config(const std::string &iniFileName) {
    m_iniPath = getExeDir() / iniFileName;
    m_recentFilePath = getExeDir() / "recent file.ini";
    initFromDisk();
}

void Config::initFromDisk() {

    // Step 1: Load existing INI entries from disk.
    bool fileExisted = fs::exists(m_iniPath);
    std::vector<std::pair<std::string, std::string>> entries;
    loadAll(entries);
    for (auto &kv : entries) {
        m_entries.insert(std::move(kv));
    }

    // Step 2: Ensure all known keys exist with valid values.
    // If the file didn't exist, this generates the default file.
    // If keys are missing or invalid, they get repaired.
    bool needSave = !fileExisted;
    needSave |= validateKey("ui_font_size",  16,         10, 24);
    needSave |= validateKey("wrap",          1,          0,  1);
    needSave |= validateKey("line_numbers",  1,          0,  1);
    needSave |= validateKey("dialog_pad",    16,         0,  64);
    needSave |= validateKey("bar_height",    32,         20, 48);
    needSave |= validateKey("btn_height",    24,         16, 40);
    needSave |= validateKey("text_bar_height", 24,      16, 32);
    needSave |= validateKey("tab_width",     4,          1,  15);
    needSave |= validateKey("auto_indent",   0,          0,  1);
    needSave |= validateKey("show_statusbar",1,          0,  1);
    needSave |= validateKey("multi_tab",     0,          0,  1);
    needSave |= validateKey("always_on_top", 0,          0,  1);
    needSave |= validateKey("long_line_marker", 0,       0,  999);
    needSave |= validateKey("highlight_current_line", 1, 0, 1);
    needSave |= validateKey("show_whitespace", 1,        0,  1);
    needSave |= validateKey("trim_trailing_whitespace", 0, 0, 1);
    needSave |= validateKey("trim_leading_blank", 0,        0,  1);
    needSave |= validateKey("trim_ending_blank", 0,        0,  1);
    needSave |= validateKey("expand_tabs_on_save", 0,      0,  1);
    needSave |= validateKey("auto_save_interval", 30,    0,  300);
    needSave |= validateKey("detect_urls",   1,          0,  1);
    needSave |= validateKey("fixed_startup_doc", 0,      0,  1);

    // AI chat configuration (endpoint URL / model / API key) - ensure the
    // keys always exist in settings.ini so the AI chat tool reads them
    // from there. The key is a string (empty by default: user must fill
    // it in); endpoint/model get their defaults.
    if (m_entries.find("ai_endpoint") == m_entries.end()) {
        m_entries["ai_endpoint"] =
            "https://open.bigmodel.cn/api/paas/v4/chat/completions";
        m_dirtyKeys.insert("ai_endpoint");
        needSave = true;
    }
    if (m_entries.find("ai_model") == m_entries.end()) {
        m_entries["ai_model"] = "glm-4-flash";
        m_dirtyKeys.insert("ai_model");
        needSave = true;
    }
    if (m_entries.find("ai_api_key") == m_entries.end()) {
        m_entries["ai_api_key"] = "";
        m_dirtyKeys.insert("ai_api_key");
        needSave = true;
    }

    // Remove deprecated keys that should no longer persist.
    // font_id/font_size: dead legacy keys (no writer ever existed);
    // font_id sat at its seeded default (4 = FL_COURIER) forever, which
    // misled users into thinking the font setting was broken. The editor
    // font's single source of truth is `editor_font`.
    m_entries.erase("ui_menu_indent_px");
    m_entries.erase("font_id");
    m_entries.erase("font_size");

    if (needSave) saveAll();

    refreshFileTime();
    recentLoad();
}

Config::~Config() {
    // Ensure the watcher thread is always stopped before destruction.
    // If startWatcher() was never called this is a no-op.
    stopWatcher();

    // Make sure any in-memory state is on disk. (saveAll already runs
    // on every write, so this is just a safety net.)
    {
        std::lock_guard<std::mutex> lock(m_ioMutex);
        if (!m_entries.empty()) saveAll();
    }
}

// ---------------------------------------------------------------------------
// File-change detection
// ---------------------------------------------------------------------------

bool Config::checkChanged() {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    std::error_code ec;
    auto t = fs::last_write_time(m_iniPath, ec);
    if (ec) return false;
    return t != m_lastWriteTime;
}

void Config::reload() {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    std::vector<std::pair<std::string, std::string>> entries;
    loadAll(entries);
    // Merge, never drop keys: the file-change notification fires while
    // another process is still writing (or right after an atomic
    // replace), so clearing m_entries first could lose keys the file
    // doesn't contain yet (e.g. lang, reverting the UI to English).
    for (auto &kv : entries) {
        if (!m_dirtyKeys.count(kv.first))
            m_entries[kv.first] = kv.second;
    }
    refreshFileTime();
}

// ---------------------------------------------------------------------------
// Window geometry
// ---------------------------------------------------------------------------

void Config::getWindow(int &x, int &y, int &w, int &h, int &flags) const {
    x = readInt("win_x", -1);
    y = readInt("win_y", -1);
    w = readInt("win_w", 900);
    h = readInt("win_h", 650);
    flags = readInt("win_flags", 0);
}

void Config::setWindow(int x, int y, int w, int h, int flags) {
    writeInt("win_x", x);
    writeInt("win_y", y);
    writeInt("win_w", w);
    writeInt("win_h", h);
    writeInt("win_flags", flags);
}

void Config::getToolSize(const char *prefix, int &w, int &h,
                         int defW, int defH) const {
    std::string keyW = std::string(prefix) + "_win_w";
    std::string keyH = std::string(prefix) + "_win_h";
    w = readInt(keyW.c_str(), defW);
    h = readInt(keyH.c_str(), defH);
}

void Config::setToolSize(const char *prefix, int w, int h) {
    std::string keyW = std::string(prefix) + "_win_w";
    std::string keyH = std::string(prefix) + "_win_h";
    writeInt(keyW.c_str(), w);
    writeInt(keyH.c_str(), h);
}

void Config::getWindowPos(int &x, int &y) const {
    x = readInt("main_win_x", 0);
    y = readInt("main_win_y", 0);
}

void Config::setWindowPos(int x, int y) {
    writeInt("main_win_x", x);
    writeInt("main_win_y", y);
}

bool Config::getWindowMax() const {
    return readInt("main_win_max", 0) != 0;
}

void Config::setWindowMax(bool on) {
    writeInt("main_win_max", on ? 1 : 0);
}

// ---------------------------------------------------------------------------
// Font
// ---------------------------------------------------------------------------

// Legacy font_id/font_size keys: FLTK font id 4 = FL_COURIER, but these
// keys never had a writer (setFont was never called by any UI), so
// font_id sat at its seeded default forever. The editor font's single
// source of truth is `editor_font` (a font NAME, View > Font); this
// getter resolves that name to a FLTK font id at call time.
namespace {
int fontNameToFlId(const char *name) {
    if (!name || !name[0]) return FL_COURIER;
    int num = Fl::set_fonts();
    for (int i = 0; i < num; ++i) {
        int attr = 0;
        const char *fn = Fl::get_font_name(i, &attr);
        if (!fn) continue;
        // ASCII case-insensitive compare (same rule as FontUtils).
        const char *a = fn, *b = name;
        while (*a && *b) {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) break;
            ++a; ++b;
        }
        if (*a == *b) return i;
    }
    // Common fallback mapping (mirrors FontUtils::fontNameToId).
    if (_stricmp(name, "Courier New") == 0 || _stricmp(name, "Courier") == 0)
        return FL_COURIER;
    if (_stricmp(name, "Helvetica") == 0) return FL_HELVETICA;
    if (_stricmp(name, "Times") == 0 || _stricmp(name, "Times New Roman") == 0)
        return FL_TIMES;
    return FL_COURIER;
}
} // namespace

void Config::getFont(int &font, int &size) const {
    char name[64];
    getEditorFont(name, sizeof(name));
    font = fontNameToFlId(name);
    // Tool-window text size follows the UI font size (the old font_size
    // key was equally dead - always its seeded default of 16).
    size = getUiFontSize();
    if (size < 6) size = 6;
    if (size > 48) size = 48;
}

void Config::setFont(int /*font*/, int /*size*/) {
    // Intentional no-op: the editor font's single source of truth is
    // `editor_font` (a font NAME, View > Font). This legacy writer used
    // to feed the dead font_id/font_size keys; kept only for the
    // SettingsProvider ABI so no caller can resurrect those keys.
}

// ---------------------------------------------------------------------------
// Scheme (theme)
// ---------------------------------------------------------------------------

void Config::getScheme(char *buf, int len, const char *fallback) const {
    const char *def = fallback ? fallback : "gtk+";
    readStr("scheme", buf, len, def);
}

void Config::setScheme(const char *name) {
    writeStr("scheme", name ? name : "gtk+");
}

// ---------------------------------------------------------------------------
// Editor options
// ---------------------------------------------------------------------------

bool Config::getWrap() const {
    return readInt("wrap", 1) != 0;
}

void Config::setWrap(bool on) {
    writeInt("wrap", on ? 1 : 0);
}

bool Config::getLineNumbers() const {
    return readInt("line_numbers", 1) != 0;
}

void Config::setLineNumbers(bool on) {
    writeInt("line_numbers", on ? 1 : 0);
}

int Config::getTabWidth() const {
    int v = readInt("tab_width", 4);
    if (v < 1) v = 1;
    if (v > 16) v = 16;
    return v;
}

void Config::setTabWidth(int w) {
    if (w < 1) w = 1;
    if (w > 16) w = 16;
    writeInt("tab_width", w);
}

bool Config::getAutoIndent() const {
    return readInt("auto_indent", 0) != 0;  // default off
}

void Config::setAutoIndent(bool on) {
    writeInt("auto_indent", on ? 1 : 0);
}

// ---------------------------------------------------------------------------
// View menu toggles
// ---------------------------------------------------------------------------

bool Config::getShowStatusbar() const {
    return readInt("show_statusbar", 1) != 0;
}

void Config::setShowStatusbar(bool on) {
    writeInt("show_statusbar", on ? 1 : 0);
}

bool Config::getShowToolbar() const {
    return readInt("show_toolbar", 1) != 0;
}

void Config::setShowToolbar(bool on) {
    writeInt("show_toolbar", on ? 1 : 0);
}

bool Config::getShowMenuBar() const {
    return readInt("show_menu_bar", 1) != 0;
}

void Config::setShowMenuBar(bool on) {
    writeInt("show_menu_bar", on ? 1 : 0);
}

bool Config::getMultiTab() const {
    return readInt("multi_tab", 0) != 0;
}

void Config::setMultiTab(bool on) {
    writeInt("multi_tab", on ? 1 : 0);
}

bool Config::getAlwaysOnTop() const {
    return readInt("always_on_top", 0) != 0;
}

void Config::setAlwaysOnTop(bool on) {
    writeInt("always_on_top", on ? 1 : 0);
}

int Config::getLongLineMarker() const {
    int v = readInt("long_line_marker", 0);
    if (v < 0) v = 0;   // sanitize
    return v;
}

void Config::setLongLineMarker(int col) {
    writeInt("long_line_marker", col < 0 ? 0 : col);
}

int Config::getDialogPad() const {
    int v = readInt("dialog_pad", 16);
    if (v < 0) v = 0;   // sanitize
    return v;
}

void Config::setDialogPad(int px) {
    writeInt("dialog_pad", px < 0 ? 0 : px);
}

int Config::getBarHeight() const {
    int v = readInt("bar_height", 32);
    if (v < 20) v = 20;
    if (v > 48) v = 48;
    return v;
}

int Config::getBtnHeight() const {
    int v = readInt("btn_height", 24);
    if (v < 16) v = 16;
    if (v > 40) v = 40;
    return v;
}

int Config::getTextBarHeight() const {
    int v = readInt("text_bar_height", 24);
    if (v < 16) v = 16;
    if (v > 32) v = 32;
    return v;
}

// Keyboard shortcuts: stored as "shortcut.<actionId>" = canonical combo
// text ("Ctrl+F", "Esc", or "" = none). Shared by all three processes
// (main window, Lua tool, AI chat) via the same settings.ini.
std::string Config::getShortcut(const char *id) const {
    char key[160];
    snprintf(key, sizeof(key), "shortcut.%s", id ? id : "");
    char buf[256] = "";
    readStr(key, buf, sizeof(buf), "");
    return std::string(buf);
}

void Config::setShortcut(const char *id, const std::string &combo) {
    char key[160];
    snprintf(key, sizeof(key), "shortcut.%s", id ? id : "");
    writeStr(key, combo.c_str());
}

bool Config::hasKey(const char *key) const {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    return key && m_entries.find(key) != m_entries.end();
}

bool Config::getHighlightCurrentLine() const {
    return readInt("highlight_current_line", 1) != 0;
}

void Config::setHighlightCurrentLine(bool on) {
    writeInt("highlight_current_line", on ? 1 : 0);
}

bool Config::getShowWhitespace() const {
    return readInt("show_whitespace", 1) != 0;
}

void Config::setShowWhitespace(bool on) {
    writeInt("show_whitespace", on ? 1 : 0);
}

bool Config::getTrimTrailingWhitespace() const {
    return readInt("trim_trailing_whitespace", 0) != 0;
}

void Config::setTrimTrailingWhitespace(bool on) {
    writeInt("trim_trailing_whitespace", on ? 1 : 0);
}

bool Config::getTrimLeadingBlank() const {
    return readInt("trim_leading_blank", 0) != 0;
}

void Config::setTrimLeadingBlank(bool on) {
    writeInt("trim_leading_blank", on ? 1 : 0);
}

bool Config::getTrimEndingBlank() const {
    return readInt("trim_ending_blank", 0) != 0;
}

void Config::setTrimEndingBlank(bool on) {
    writeInt("trim_ending_blank", on ? 1 : 0);
}

bool Config::getExpandTabsOnSave() const {
    return readInt("expand_tabs_on_save", 0) != 0;
}

void Config::setExpandTabsOnSave(bool on) {
    writeInt("expand_tabs_on_save", on ? 1 : 0);
}

int Config::getAutoSaveInterval() const {
    int v = readInt("auto_save_interval", 30);  // default 30s
    if (v < 0) v = 0;
    if (v > 3600) v = 3600;
    return v;
}

void Config::setAutoSaveInterval(int secs) {
    if (secs < 0) secs = 0;
    if (secs > 3600) secs = 3600;
    writeInt("auto_save_interval", secs);
}

bool Config::getDetectUrls() const {
    return readInt("detect_urls", 1) != 0;  // default on
}

// 退出时清理 7 天前的临时文件（build/temp 残留）
bool Config::getCleanupTempOld() const {
    return readInt("cleanup_temp_old", 1) != 0;  // default on
}

void Config::setCleanupTempOld(bool on) {
    writeInt("cleanup_temp_old", on ? 1 : 0);
}

// 固定启动文档（见 Config.h）：无其它 Pecia 进程时启动直接打开 Test.txt
bool Config::getFixedStartupDoc() const {
    return readInt("fixed_startup_doc", 0) != 0;  // default off
}

void Config::setFixedStartupDoc(bool on) {
    writeInt("fixed_startup_doc", on ? 1 : 0);
}

void Config::getOpenWithExts(char *buf, int len, const char *fallback) const {
    readStr("open_with_exts", buf, len, fallback);
}

void Config::setOpenWithExts(const char *exts) {
    writeStr("open_with_exts", exts ? exts : "");
}

int Config::getUiFontSize() const {
    return readInt("ui_font_size", 16);  // default 16
}

void Config::setUiFontSize(int size) {
    if (size < 10) size = 10;
    if (size > 24) size = 24;
    writeInt("ui_font_size", size);
}

int Config::getPreviewAutoRefreshMs() const {
    int v = readInt("preview_auto_refresh", 5000);  // default 5s
    // 校验合法档位（防御历史垃圾值，如曾误写入的指针垃圾）
    switch (v) {
    case 0: case 1000: case 5000: case 10000: case 30000:
        return v;
    default:
        return 5000;
    }
}

void Config::setPreviewAutoRefreshMs(int ms) {
    if (ms < 0) ms = 0;
    writeInt("preview_auto_refresh", ms);
}

bool Config::getPreviewScrollSync() const {
    return readInt("preview_scroll_sync", 0) != 0;  // default off
}

void Config::setPreviewScrollSync(bool on) {
    writeInt("preview_scroll_sync", on ? 1 : 0);
}

void Config::setDetectUrls(bool on) {
    writeInt("detect_urls", on ? 1 : 0);
}

void Config::getEditorFont(char *buf, int len, const char *fallback) const {
    const char *def = fallback ? fallback : "Consolas";
    readStr("editor_font", buf, len, def);
}

void Config::setEditorFont(const char *name) {
    writeStr("editor_font", name ? name : "Consolas");
}

int Config::getEditorZoomSize() const {
    // 0 = 从未缩放（跟随 ui_font_size 基准）。
    return readInt("editor_zoom_size", 0);
}

void Config::setEditorZoomSize(int size) {
    writeInt("editor_zoom_size", size);
}

// 搜索选中内容的搜索引擎 URL 模板（%s = 选中内容，默认 Bing）
void Config::getSearchEngineUrl(char *buf, int len) const {
    readStr("search_engine_url", buf, len,
            "https://www.bing.com/search?q=%s");
}

void Config::setSearchEngineUrl(const char *url) {
    writeStr("search_engine_url",
             (url && *url) ? url : "https://www.bing.com/search?q=%s");
}

// ---------------------------------------------------------------------------
// Language
// ---------------------------------------------------------------------------

void Config::getLang(char *buf, int len, const char *fallback) const {
    const char *def = fallback ? fallback : "en";
    readStr("lang", buf, len, def);
    // Accept ANY non-empty stored language code. The set of real languages
    // is dynamic (lang/*.ini, scanned by I18n::scanLanguages()), so this
    // must NOT hard-code a whitelist -- previously only {"en","zh-CN"} were
    // allowed, which silently reset every other added language to English
    // on restart. Whether a stored code actually has a matching file is
    // validated later by I18n::load(), which falls back to English when the
    // file is missing. We only guard against an empty value here.
    if (buf[0] == '\0') fl_strlcpy(buf, def, len);
}

void Config::setLang(const char *code) {
    writeStr("lang", code ? code : "en");
}

void Config::getAiKey(char *buf, int len, const char *fallback) const {
    readStr("ai_api_key", buf, len, fallback ? fallback : "");
}

void Config::setAiKey(const char *key) {
    writeStr("ai_api_key", key ? key : "");
}

void Config::getAiModel(char *buf, int len, const char *fallback) const {
    readStr("ai_model", buf, len, fallback ? fallback : "glm-4-flash");
}

void Config::setAiModel(const char *model) {
    writeStr("ai_model", model ? model : "glm-4-flash");
}

void Config::getAiEndpoint(char *buf, int len, const char *fallback) const {
    readStr("ai_endpoint", buf, len,
            fallback ? fallback : "https://open.bigmodel.cn/api/paas/v4/chat/completions");
}

void Config::setAiEndpoint(const char *url) {
    writeStr("ai_endpoint",
             url ? url : "https://open.bigmodel.cn/api/paas/v4/chat/completions");
}

// ---------------------------------------------------------------------------
// Last directory
// ---------------------------------------------------------------------------

void Config::getLastDir(char *buf, int len, const char *fallback) const {
    readStr("last_dir", buf, len, fallback ? fallback : "");
}

void Config::setLastDir(const char *dir) {
    writeStr("last_dir", dir ? dir : "");
}

// ---------------------------------------------------------------------------
// Recent files (max MAX_RECENT, most recent first)
//
// Stored in a separate "recent file.ini" next to the exe. The file
// format is:
//
//     File1=/absolute/path/to/file1.txt
//     File2=/absolute/path/to/file2.txt
//     ...
//
// Lines starting with '#' and blank lines are ignored. Any other line
// that does not match "File<digits>=<value>" causes the file to be
// treated as malformed: it is deleted and a fresh empty file is
// created. Entries with an empty value are silently dropped on load.
// ---------------------------------------------------------------------------

void Config::recentLoad() {
    m_recentFiles.clear();

    FILE *fp = fl_fopen(m_recentFilePath.string().c_str(), "r");
    if (!fp) {
        recentSave();
        return;
    }

    char line[FL_PATH_MAX + 64];
    bool malformed = false;
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = 0;
        }

        if (len == 0) continue;

        const char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#') continue;

        if (strncmp(p, "File", 4) != 0) { malformed = true; break; }
        p += 4;

        const char *digitStart = p;
        while (*p >= '0' && *p <= '9') ++p;
        if (p == digitStart) { malformed = true; break; }

        if (*p != '=') { malformed = true; break; }
        ++p;

        if (*p) {
            m_recentFiles.push_back(std::string(p));
        }
    }
    fclose(fp);

    if (malformed) {
        fl_unlink(m_recentFilePath.string().c_str());
        m_recentFiles.clear();
        recentSave();
        return;
    }

    if ((int)m_recentFiles.size() > MAX_RECENT) {
        m_recentFiles.resize(MAX_RECENT);
    }
}

void Config::recentSave() {
    FILE *fp = fl_fopen(m_recentFilePath.string().c_str(), "w");
    if (!fp) return;
    for (size_t i = 0; i < m_recentFiles.size(); ++i) {
        fprintf(fp, "File%d=%s\n", (int)(i + 1), m_recentFiles[i].c_str());
    }
    fclose(fp);
}

int Config::recentCount() {
    return (int)m_recentFiles.size();
}

void Config::recentGet(int index, char *buf, int len) {
    if (index < 0 || index >= (int)m_recentFiles.size()) {
        if (len > 0) buf[0] = 0;
        return;
    }
    fl_strlcpy(buf, m_recentFiles[index].c_str(), len);
}

void Config::recentAdd(const char *path) {
    if (!path || !path[0]) return;

    for (auto it = m_recentFiles.begin(); it != m_recentFiles.end(); ) {
        if (*it == path) it = m_recentFiles.erase(it);
        else ++it;
    }

    m_recentFiles.insert(m_recentFiles.begin(), std::string(path));

    if ((int)m_recentFiles.size() > MAX_RECENT) {
        m_recentFiles.resize(MAX_RECENT);
    }

    recentSave();
}

void Config::recentClear() {
    m_recentFiles.clear();
    recentSave();
}

// ---------------------------------------------------------------------------
// File-change callback handler (called via Fl::awake from watcher thread)
// ---------------------------------------------------------------------------

static void changeCallbackHandler(void *data) {
    Config *config = static_cast<Config *>(data);
    if (config && config->hasChangeCallback()) {
        config->invokeChangeCallback();
    }
}

// ---------------------------------------------------------------------------
// File-change watcher (ReadDirectoryChangesW on Windows)
// ---------------------------------------------------------------------------

void Config::setChangeCallback(std::function<void()> callback) {
    m_changeCallback = std::move(callback);
}

#if defined(_WIN32)

#include <FL/Fl.H>

void Config::startWatcher() {
    if (m_watcherRunning.load()) return;
    m_watcherRunning.store(true);
    m_watcherThread = std::thread(&Config::watcherThreadFunc, this);
}

void Config::stopWatcher() {
    m_watcherRunning.store(false);
    if (m_watcherThread.joinable()) {
        m_watcherThread.join();
    }
}

void Config::watcherThreadFunc() {
    // Use the wide-char API: the old FindFirstChangeNotificationA mangles
    // UTF-8/wide paths, so hot-reload silently never fired when Pecia sat
    // in a directory whose name contains non-ASCII characters.
    std::wstring dirPath = m_iniPath.parent_path().wstring();
    HANDLE hChange = FindFirstChangeNotificationW(
        dirPath.c_str(),
        FALSE,
        FILE_NOTIFY_CHANGE_LAST_WRITE
    );

    if (hChange == INVALID_HANDLE_VALUE) {
        return;
    }

    while (m_watcherRunning.load()) {
        DWORD waitResult = WaitForSingleObject(hChange, 1000);
        if (waitResult == WAIT_OBJECT_0) {
            if (checkChanged()) {
                reload();
                reloadFromDisk();   // refresh the in-memory cache too
                if (hasChangeCallback()) {
                    Fl::awake(changeCallbackHandler, this);
                }
            }
            FindNextChangeNotification(hChange);
        }
    }

    FindCloseChangeNotification(hChange);
}

#else

// POSIX fallback: simple polling
void Config::startWatcher() {}
void Config::stopWatcher() {}

#endif
