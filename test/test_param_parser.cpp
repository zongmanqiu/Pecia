// test_param_parser.cpp - unit tests for the --!param / --!pui header
// parser (parameter declarations + script-carried dialog translations).
#include "test_assert.h"
#include "script/LuaParamParser.h"

#include <cstring>
#include <string>

static void test_no_params() {
    LuaParamSet p = luaParseParams("print('hi')");
    CHECK(p.params.empty());
}

static void test_basic_param() {
    LuaParamSet p =
        luaParseParams("--!param prefix: 前缀\nprint(params.prefix)");
    CHECK_EQ((int)p.params.size(), 1);
    CHECK(strcmp(p.params[0].name.c_str(), "prefix") == 0);
    CHECK(strcmp(p.params[0].defValue.c_str(), "") == 0);
    CHECK(strcmp(p.params[0].description.c_str(), "前缀") == 0);
}

static void test_default_value() {
    LuaParamSet p =
        luaParseParams("--!param count=3: 重复次数");
    CHECK_EQ((int)p.params.size(), 1);
    CHECK(strcmp(p.params[0].name.c_str(), "count") == 0);
    CHECK(strcmp(p.params[0].defValue.c_str(), "3") == 0);
    CHECK(strcmp(p.params[0].description.c_str(), "重复次数") == 0);
}

static void test_no_description() {
    LuaParamSet p = luaParseParams("--!param name=x");
    CHECK_EQ((int)p.params.size(), 1);
    CHECK(strcmp(p.params[0].name.c_str(), "name") == 0);
    CHECK(strcmp(p.params[0].defValue.c_str(), "x") == 0);
    CHECK(strcmp(p.params[0].description.c_str(), "") == 0);
}

static void test_multiple_params() {
    LuaParamSet p = luaParseParams(
        "--!param a=1: A\n--!param b: B\n--!param c: C");
    CHECK_EQ((int)p.params.size(), 3);
    CHECK(strcmp(p.params[0].name.c_str(), "a") == 0);
    CHECK(strcmp(p.params[1].name.c_str(), "b") == 0);
    CHECK(strcmp(p.params[2].name.c_str(), "c") == 0);
    CHECK(strcmp(p.params[0].defValue.c_str(), "1") == 0);
}

static void test_invalid_ident_skipped() {
    // "1bad" is not a valid Lua identifier - declaration is skipped.
    LuaParamSet p = luaParseParams(
        "--!param 1bad: x\n--!param ok: y");
    CHECK_EQ((int)p.params.size(), 1);
    CHECK(strcmp(p.params[0].name.c_str(), "ok") == 0);
}

static void test_declaration_after_code_ignored() {
    // The header area ends at the first real Lua code line - a
    // declaration after code is not part of the header.
    LuaParamSet p = luaParseParams(
        "--!param ok: first\n"
        "local x = 1\n"
        "--!param late: after code\n");
    CHECK_EQ((int)p.params.size(), 1);
    CHECK(strcmp(p.params[0].name.c_str(), "ok") == 0);
}

static void test_many_declarations_parsed() {
    // Large declaration sets (meta + params + translations) must all fit:
    // the scan is bounded by the first code line, not a line count.
    std::string script;
    for (int i = 1; i <= 60; ++i)
        script += "--!param p" + std::to_string(i) + "=v" +
                  std::to_string(i) + ": param " + std::to_string(i) + "\n";
    script += "local x = 1\n";
    LuaParamSet p = luaParseParams(script);
    CHECK_EQ((int)p.params.size(), 60);
    CHECK(strcmp(p.params[59].name.c_str(), "p60") == 0);
}

static void test_comment_not_param() {
    // A plain comment is not a parameter declaration.
    LuaParamSet p = luaParseParams("-- this is a comment\nprint(1)");
    CHECK(p.params.empty());
}

static void test_blank_lines_before() {
    LuaParamSet p =
        luaParseParams("\n\n--!param afterBlank: ok\n");
    CHECK_EQ((int)p.params.size(), 1);
    CHECK(strcmp(p.params[0].name.c_str(), "afterBlank") == 0);
}

static void test_choice_dropdown() {
    // A '|' inside the default turns the parameter into a dropdown:
    // choices = {before, after}, default = first option.
    LuaParamSet p = luaParseParams(
        "--!param where=before|after: pad position");
    CHECK_EQ((int)p.params.size(), 1);
    CHECK_EQ((int)p.params[0].choices.size(), 2);
    CHECK(strcmp(p.params[0].choices[0].c_str(), "before") == 0);
    CHECK(strcmp(p.params[0].choices[1].c_str(), "after") == 0);
    CHECK(strcmp(p.params[0].defValue.c_str(), "before") == 0);
    CHECK(strcmp(p.params[0].description.c_str(), "pad position") == 0);
}

static void test_choice_three_options() {
    LuaParamSet p = luaParseParams(
        "--!param align=left|center|right: alignment");
    CHECK_EQ((int)p.params[0].choices.size(), 3);
    CHECK(strcmp(p.params[0].choices[2].c_str(), "right") == 0);
    CHECK(strcmp(p.params[0].defValue.c_str(), "left") == 0);
}

static void test_choice_with_text_param_mixed() {
    LuaParamSet p = luaParseParams(
        "--!param where=before|after: pos\n--!param width=4: digits");
    CHECK_EQ((int)p.params.size(), 2);
    CHECK_EQ((int)p.params[0].choices.size(), 2);
    CHECK(p.params[1].choices.empty());
    CHECK(strcmp(p.params[1].defValue.c_str(), "4") == 0);
}

static void test_onoff_becomes_checkbox() {
    // An on|off (or true|false) parameter is a boolean switch -> the
    // dialog renders a checkbox like the find bar's Match Case.
    LuaParamSet p = luaParseParams("--!param case=on|off: case sensitive");
    CHECK(p.params[0].isCheckbox);
    CHECK_EQ((int)p.params[0].choices.size(), 2);
    CHECK(strcmp(p.params[0].defValue.c_str(), "on") == 0);

    LuaParamSet t = luaParseParams("--!param wrap=true|false");
    CHECK(t.params[0].isCheckbox);
    CHECK(strcmp(t.params[0].defValue.c_str(), "true") == 0);

    // Non-boolean two-option lists stay dropdowns.
    LuaParamSet w = luaParseParams("--!param where=before|after");
    CHECK(!w.params[0].isCheckbox);
}

static void test_pui_bilingual_resolution() {
    // Script carries its own dialog translations; parsing for zh-CN
    // must pick the language overrides, en/base otherwise.
    const std::string script =
        "--!param where=before|after\n"
        "--!param width=4\n"
        "--!pui.title = Pad Numbers\n"
        "--!pui.title.zh-CN = 数字补0\n"
        "--!pui.where = Where to pad\n"
        "--!pui.where.zh-CN = 补0位置\n"
        "--!pui.where.before = Integer part\n"
        "--!pui.where.before.zh-CN = 整数部分\n"
        "--!pui.width = Target digits\n";

    LuaParamSet zh = luaParseParams(script, "zh-CN");
    CHECK(strcmp(zh.title.c_str(), "数字补0") == 0);
    CHECK_EQ((int)zh.params.size(), 2);
    CHECK(strcmp(zh.params[0].description.c_str(), "补0位置") == 0);
    CHECK_EQ((int)zh.params[0].choiceLabels.size(), 2);
    CHECK(strcmp(zh.params[0].choiceLabels[0].c_str(), "整数部分") == 0);
    // "after" has no pui entry at all -> falls back to the raw option
    // value ("after" is already English, matching the en-default rule).
    CHECK(strcmp(zh.params[0].choiceLabels[1].c_str(), "after") == 0);
    CHECK(strcmp(zh.params[1].description.c_str(), "Target digits") == 0);

    LuaParamSet en = luaParseParams(script, "en");
    CHECK(strcmp(en.title.c_str(), "Pad Numbers") == 0);
    CHECK(strcmp(en.params[0].description.c_str(), "Where to pad") == 0);
    CHECK(strcmp(en.params[0].choiceLabels[0].c_str(), "Integer part") == 0);

    // No language given -> base texts, and untranslated params keep
    // their raw defaults (name/option value).
    LuaParamSet raw = luaParseParams("--!param width=4");
    CHECK(raw.title.empty());
    CHECK(strcmp(raw.params[0].description.c_str(), "") == 0);
    CHECK(strcmp(raw.params[0].defValue.c_str(), "4") == 0);
}

static void test_crlf_line_endings() {
    // Windows-edited (CRLF) scripts used to leak '\r' into values:
    // "--!param width=4\r\n" parsed width as "4\r" (shows as "4^M"
    // and breaks tonumber()). Trailing CR must now be stripped.
    std::string script =
        "--!param where=before|after: pos\r\n"
        "--!param width=4: digits\r\n"
        "local x = 1\r\n";
    LuaParamSet p = luaParseParams(script, "en");
    CHECK_EQ((int)p.params.size(), 2);
    CHECK(strcmp(p.params[0].name.c_str(), "where") == 0);
    CHECK_EQ((int)p.params[0].choices.size(), 2);
    CHECK(strcmp(p.params[0].choices[0].c_str(), "before") == 0);
    CHECK(strcmp(p.params[0].choices[1].c_str(), "after") == 0);
    CHECK(strcmp(p.params[0].defValue.c_str(), "before") == 0);
    CHECK(strcmp(p.params[1].name.c_str(), "width") == 0);
    CHECK(strcmp(p.params[1].defValue.c_str(), "4") == 0);
    CHECK(strcmp(p.params[1].description.c_str(), "digits") == 0);
}

static void test_pui_extra_language_override() {
    // A language outside the built-in base set (e.g. ja) is recognized
    // only when the caller passes it via knownLangs; its --!pui.<code>
    // override must then resolve, else script translations for added
    // languages silently fall back to English.
    const std::string script =
        "--!param width=4\n"
        "--!pui.title = Pad Numbers\n"
        "--!pui.title.ja = 数字補0\n"
        "--!pui.width = Target digits\n"
        "--!pui.width.ja = 桁数\n";
    // ja not supplied -> override ignored, base used.
    LuaParamSet en = luaParseParams(script, "ja");
    CHECK(strcmp(en.title.c_str(), "Pad Numbers") == 0);
    CHECK(strcmp(en.params[0].description.c_str(), "Target digits") == 0);
    // ja supplied -> override resolves.
    LuaParamSet ja = luaParseParams(script, "ja", { "ja" });
    CHECK(strcmp(ja.title.c_str(), "数字補0") == 0);
    CHECK(strcmp(ja.params[0].description.c_str(), "桁数") == 0);
}

static void run_all() {
    test_no_params();
    test_basic_param();
    test_default_value();
    test_no_description();
    test_multiple_params();
    test_invalid_ident_skipped();
    test_declaration_after_code_ignored();
    test_many_declarations_parsed();
    test_comment_not_param();
    test_blank_lines_before();
    test_choice_dropdown();
    test_choice_three_options();
    test_choice_with_text_param_mixed();
    test_onoff_becomes_checkbox();
    test_pui_bilingual_resolution();
    test_crlf_line_endings();
    test_pui_extra_language_override();
}

int main() {
    run_all();
    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n",
                 test::checkCount(), fails);
    return fails ? 1 : 0;
}
