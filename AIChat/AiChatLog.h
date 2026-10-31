// AiChatLog.h - ai_chat.txt 的序列化/解析（纯函数，供单测直接覆盖）。
//
// AI 对话历史的落盘格式（照 Lua 控制台 script.last.txt 的模式：exe 同级纯
// 文本、宽字符路径兼容中文安装目录）。文件是**追加式日志**：每条消息发送/
// 收到时立即追加，"清除"按钮才清空——不点清除就不消失。
//
// 每条一个块：
//     [user 2026-10-31 12:00:00]
//     内容（可多行）
//
//     [assistant ...]
//     ...
// 标记行内的角色后可带时间戳（加载时原样交回，供 UI 恢复显示）。
#pragma once

#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace ai_chat_log {

// 日志文件路径：exe 同级 ai_chat.txt。主窗口（Pecia.exe）与 AI 窗口
// （PeciaAIChat.exe）**共用同一个文件**——文件即对话状态：主窗口把选中文本
// 作为 user 记录直接追加进来，AI 窗口轮询文件变化并以其为上下文发起请求。
// 两个 exe 始终安装在同一目录（AI 窗口由主窗口从自己旁边拉起），所以按各自
// 的 exe 位置推导出的路径必然相同。
//
// 文件名是单后缀 ai_chat.txt（旧版叫 ai_chat.log.txt，两个后缀读起来别扭）。

// 旧名 ai_chat.log.txt → 新名 ai_chat.txt 的一次性迁移（进程内只做一次）。
// 返回 true = 新名可用（已迁移、本来就用新名、或没有历史文件）。
// 迁移失败（旧文件被别的进程占用打开）时返回 false，调用方这一轮继续用旧名
// —— 两个 exe 看到的是同一份文件，宁可名字难看，也不能分裂成两套历史。
inline bool migrateLegacyLogName(const std::wstring &dir) {
    const std::wstring nu = dir + L"\\ai_chat.txt";
    const std::wstring old = dir + L"\\ai_chat.log.txt";
    if (GetFileAttributesW(nu.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    if (GetFileAttributesW(old.c_str()) == INVALID_FILE_ATTRIBUTES) return true;
    return MoveFileW(old.c_str(), nu.c_str()) != 0;
}

inline std::wstring defaultLogPath() {
    wchar_t exePath[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"ai_chat.txt";
    wchar_t *slash = wcsrchr(exePath, L'\\');
    if (slash) *slash = 0;
    const std::wstring dir(exePath);
    static const bool legacyMigrated = migrateLegacyLogName(dir);
    return legacyMigrated ? dir + L"\\ai_chat.txt"
                          : dir + L"\\ai_chat.log.txt";
}

struct Entry {
    std::string role;     // "user" / "assistant"
    std::string time;     // "2026-10-31 12:00:00"；旧条目/缺失时为空
    std::string content;  // 可多行（不含结尾换行）
};

// 一条记录 → 文本块（追加进文件的最小单位）。
inline std::string serializeTurn(const char *role, const char *ts,
                                 const std::string &text) {
    std::string out = std::string("[") + (role ? role : "");
    if (ts && *ts) { out += ' '; out += ts; }
    out += "]\n";
    out += text;
    out += "\n\n";
    return out;
}

// 全文件 → 条目列表。容错哲学与 recent file.ini 一致：手工编辑/半截写入
// 不能毁掉整份历史——只认 [user]/[assistant] 开新条目，其它一切行按内容
// 处理（包括未知的 [xxx] 行），空条目跳过，UTF-8 BOM 剥离。
inline std::vector<Entry> parseChatLog(const std::string &raw) {
    std::vector<Entry> out;
    std::string body = raw;
    if (body.size() >= 3 && static_cast<unsigned char>(body[0]) == 0xEF &&
        static_cast<unsigned char>(body[1]) == 0xBB &&
        static_cast<unsigned char>(body[2]) == 0xBF)
        body.erase(0, 3);

    Entry cur;
    bool inEntry = false;
    auto flush = [&]() {
        if (inEntry && !cur.role.empty()) {
            // 块与块之间的空行会以尾部换行的形式并入内容，剥掉；条目内部的
            // 空行（用户消息里的空段落）必须原样保留。
            while (!cur.content.empty() &&
                   (cur.content.back() == '\n' || cur.content.back() == '\r'))
                cur.content.pop_back();
            if (!cur.content.empty()) out.push_back(cur);
        }
        cur = Entry();
        inEntry = false;
    };

    size_t pos = 0;
    while (pos <= body.size()) {
        size_t eol = body.find('\n', pos);
        std::string line = body.substr(
            pos, (eol == std::string::npos ? body.size() : eol) - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        bool isMarker = false;
        if (line.size() >= 2 && line[0] == '[') {
            size_t close = line.find(']');
            if (close != std::string::npos) {
                std::string tag = line.substr(1, close - 1);
                size_t sp = tag.find(' ');
                std::string role =
                    sp == std::string::npos ? tag : tag.substr(0, sp);
                if (role == "user" || role == "assistant") {
                    flush();
                    cur.role = role;
                    cur.time = sp == std::string::npos ? "" : tag.substr(sp + 1);
                    inEntry = true;
                    isMarker = true;
                }
            }
        }
        if (!isMarker && inEntry) {
            if (!cur.content.empty()) cur.content += '\n';
            cur.content += line;
        }
        if (eol == std::string::npos) break;
        pos = eol + 1;
    }
    flush();
    return out;
}

} // namespace ai_chat_log
