-- ==Meta==
-- @name Regex Replace
-- @name.zh-CN 正则替换
-- @author 邱宗满
-- @date 20260810
--!param pattern=: regex pattern (PCRE2 syntax)
--!param replacement=: replacement text ($1 = first capture group)
--!pui.title = Regex Replace
--!pui.title.zh-CN = 正则替换
--!pui.pattern = Find
--!pui.pattern.zh-CN = 查找
--!pui.replacement = Replacement
--!pui.replacement.zh-CN = 替换为
-- ==/Meta==

-- Replace every match of a PCRE2 regex in the selection. The engine is
-- pecia_regex.dll, loaded on demand and unloaded when the script ends.
--
-- PCRE2 syntax (not Lua patterns):
--   \d+        digits        [0-9]{2,4}   bounded repeat
--   (?i)foo    case-insensitive
--   (a|b)      alternation   $1           capture group in replacement
--   \p{Han}+   Chinese chars
-- Case sensitivity follows standard regex rules (sensitive by default);
-- prepend (?i) to the pattern to ignore case.

local sel = editor:get_selection()
if #sel == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the text to process first.")
    end
    return
end

local pattern = params.pattern or ""
local replacement = params.replacement or ""
if pattern == "" then
    if pecia_lang == "zh-CN" then
        print("请填写正则模式。")
    else
        print("Pattern is empty - fill in a regex pattern first.")
    end
    return
end

local r, err = regex.gsub(sel, pattern, replacement)
if r == nil then
    if pecia_lang == "zh-CN" then
        print("正则错误: " .. tostring(err))
    else
        print("Regex error: " .. tostring(err))
    end
    return
end
if r ~= sel then
    editor:replace_selection(r)
else
    if pecia_lang == "zh-CN" then
        print("无匹配。")
    else
        print("No matches.")
    end
end
