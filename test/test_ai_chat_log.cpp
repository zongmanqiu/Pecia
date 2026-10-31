// test_ai_chat_log.cpp - AI 对话历史落盘格式（AIChat/AiChatLog.h）。
//
// 守的是"历史文件"的往返契约：serialize → parse 必须还原（多行内容、内部
// 空行、时间戳）；解析必须容错（BOM、手工编辑、半截写入、未知标记行）——
// 历史文件坏了不能毁掉整份记录，这是 recent file.ini 一脉的哲学。

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "AIChat/AiChatLog.h"
#include "test/test_assert.h"

namespace {

bool blankOf(const std::vector<ai_chat_log::Entry> &v) { return v.empty(); }

void test_roundtrip() {
    std::string file;
    file += ai_chat_log::serializeTurn("user", "2026-10-31 12:00:00", "你好");
    file += ai_chat_log::serializeTurn("assistant", "2026-10-31 12:00:05",
                                       "第一行\n第二行\n\n第三行");
    file += ai_chat_log::serializeTurn("user", "", "no timestamp");

    auto v = ai_chat_log::parseChatLog(file);
    CHECK_EQ(v.size(), 3);

    CHECK(v[0].role == "user");
    CHECK(v[0].time == "2026-10-31 12:00:00");
    CHECK(v[0].content == "你好");

    CHECK(v[1].role == "assistant");
    // 内部空行必须原样保留（用户消息里的空段落），仅块间分隔被剥掉
    CHECK(v[1].content == "第一行\n第二行\n\n第三行");

    CHECK(v[2].role == "user");
    CHECK(v[2].time.empty());
    CHECK(v[2].content == "no timestamp");
}

void test_multiline_and_internal_blank_lines() {
    std::string file = ai_chat_log::serializeTurn(
        "user", "t", "段落一\n\n段落二\n\n\n段落三");
    auto v = ai_chat_log::parseChatLog(file);
    CHECK_EQ(v.size(), 1);
    CHECK(v[0].content == "段落一\n\n段落二\n\n\n段落三");
}

void test_bom_and_empty_file() {
    CHECK(blankOf(ai_chat_log::parseChatLog("")));
    CHECK(blankOf(ai_chat_log::parseChatLog("\xEF\xBB\xBF")));
    CHECK(blankOf(ai_chat_log::parseChatLog("\xEF\xBB\xBF[user]\n\n")));
    // BOM 后的正常内容照常解析
    std::string file = "\xEF\xBB\xBF" + ai_chat_log::serializeTurn("user", "", "hi");
    auto v = ai_chat_log::parseChatLog(file);
    CHECK_EQ(v.size(), 1);
    CHECK(v[0].content == "hi");
}

void test_tolerates_hand_edits() {
    // 半截写入（最后一块没有收尾空行）：能恢复出已写入的内容
    std::string file = ai_chat_log::serializeTurn("user", "", "完整的一条") +
                       "[assistant 2026-01-02 03:04:05]\n写了一半";
    auto v = ai_chat_log::parseChatLog(file);
    CHECK_EQ(v.size(), 2);
    CHECK(v[1].role == "assistant");
    CHECK(v[1].time == "2026-01-02 03:04:05");
    CHECK(v[1].content == "写了一半");

    // 未知标记行：按内容处理，不毁掉当前条目
    std::string file2 = ai_chat_log::serializeTurn("user", "", "正文\n[system]\n仍算正文");
    auto v2 = ai_chat_log::parseChatLog(file2);
    CHECK_EQ(v2.size(), 1);
    CHECK(v2[0].content == "正文\n[system]\n仍算正文");

    // 空条目（标记后没有内容）被跳过
    std::string file3 = "[user]\n\n[assistant]\nok\n";
    auto v3 = ai_chat_log::parseChatLog(file3);
    CHECK_EQ(v3.size(), 1);
    CHECK(v3[0].role == "assistant");
    CHECK(v3[0].content == "ok");
}

void test_timestamp_formats() {
    // 无时间戳的标记（旧格式/手写）照常解析
    auto v = ai_chat_log::parseChatLog("[user]\nabc\n");
    CHECK_EQ(v.size(), 1);
    CHECK(v[0].time.empty());
    CHECK(v[0].content == "abc");

    // 角色与时间戳之间以第一个空格分隔；时间戳本身含空格
    auto v2 = ai_chat_log::parseChatLog("[assistant 2026-01-02 03:04:05]\nX\n");
    CHECK_EQ(v2.size(), 1);
    CHECK(v2[0].role == "assistant");
    CHECK(v2[0].time == "2026-01-02 03:04:05");
}

// 旧名 ai_chat.log.txt → 新名 ai_chat.txt 的一次性迁移：历史必须原样搬过去，
// 新名已存在时绝不覆盖（两个进程同时启动时后到者不能把已有历史盖掉），
// 没有旧文件时不凭空造文件。
void test_legacy_name_migration() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / "pecia_ai_chat_migrate";
    if (ec) { CHECK(false); return; }
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path legacy = dir / "ai_chat.log.txt";
    const fs::path renamed = dir / "ai_chat.txt";
    auto write = [](const fs::path &p, const char *text, bool append) {
        std::ofstream o(p, std::ios::binary | (append ? std::ios::app
                                                     : std::ios::trunc));
        o << text;
    };
    auto read = [](const fs::path &p) {
        std::ifstream i(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(i)),
                           std::istreambuf_iterator<char>());
    };

    // 1) 只有旧名 → 改名，内容一个字都不能丢
    write(legacy, "[user]\nhi\n", false);
    CHECK(ai_chat_log::migrateLegacyLogName(dir.wstring()));
    CHECK(fs::exists(renamed));
    CHECK(!fs::exists(legacy));
    CHECK(read(renamed) == "[user]\nhi\n");

    // 2) 两个都在 → 不覆盖新名，仍返回 true（新名优先，旧文件留给用户自己删）
    write(legacy, "[user]\nold\n", false);
    CHECK(ai_chat_log::migrateLegacyLogName(dir.wstring()));
    CHECK(read(renamed) == "[user]\nhi\n");
    CHECK(fs::exists(legacy));

    // 3) 谁都没有 → 返回 true 但不创建文件（首次运行的常态）
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    CHECK(ai_chat_log::migrateLegacyLogName(dir.wstring()));
    CHECK(!fs::exists(renamed));
    CHECK(!fs::exists(legacy));

    fs::remove_all(dir, ec);
}

} // namespace

int main() {
    test_roundtrip();
    test_multiline_and_internal_blank_lines();
    test_bom_and_empty_file();
    test_tolerates_hand_edits();
    test_timestamp_formats();
    test_legacy_name_migration();

    const int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n", test::checkCount(), fails);
    return fails ? 1 : 0;
}
