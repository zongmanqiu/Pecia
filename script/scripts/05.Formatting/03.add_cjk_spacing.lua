-- ==Meta==
-- @name CJK-Latin Spacing
-- @name.zh-CN 文本混排间隔
-- ==/Meta==

-- 在中文（CJK）与英文/数字之间自动添加 1 个空格，提升混排阅读体验。
-- 规则（PCRE2，UCP 模式）：
--   * 汉字后紧跟字母/数字 -> 中间加空格（如：你好world -> 你好 world）
--   * 字母/数字后紧跟汉字 -> 中间加空格（如：Hello世界 -> Hello 世界）
-- 标点（。，、；：！？等全角标点，以及半角 , . ; : ! ?）不会加空格。
-- 全角空格（U+3000）视为正常空格，不会重复添加。

local sel = editor:get_selection()
if #sel == 0 then
    print("No selection - select the text to process first.")
    return
end

local out, err = regex.gsub(sel, "(\\p{Han})(?=[A-Za-z0-9])", "$1 ")
if out == nil then
    print("Regex error: " .. tostring(err))
    return
end
out, err = regex.gsub(out, "([A-Za-z0-9])(?=\\p{Han})", "$1 ")
if out == nil then
    print("Regex error: " .. tostring(err))
    return
end

if out ~= sel then
    editor:replace_selection(out)
    print("Inserted " .. #out - #sel .. " spacing character(s).")
else
    print("No CJK-Latin boundaries found.")
end
