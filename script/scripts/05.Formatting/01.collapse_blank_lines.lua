-- ==Meta==
-- @name Blank Line Spacing
-- @name.zh-CN 空行间隔
-- ==/Meta==

-- 行与行之间保持恰好 1 行空行间隔：
--   * 两个非空行之间没有空行 -> 插入 1 个空行
--   * 多个连续空行 -> 删减到 1 个空行
--   * 恰好 1 个空行 -> 保持不变
-- 保留原文换行风格（\r\n / \n）与行首缩进。

local sel = editor:get_selection()
if #sel == 0 then
    print("No selection - select the text to process first.")
    return
end

local crlf = sel:find("\r\n") ~= nil

-- 归一化为 \n 统一处理，最后按原风格还原
local out = sel:gsub("\r\n", "\n")
-- 1) 多余空行（3 个及以上换行，含空行内空白）-> 1 个空行
out = out:gsub("\n[ \t]*\n[ \t]*\n+", "\n\n")
-- 2) 非空行之间没有空行 -> 插入 1 个空行（保留行首缩进）。匹配
--    “非空行结尾 + 换行 + 下行开头”，不会命中已经是空行后的换行。
--    单遍 gsub 不重叠：单字符行被前一个匹配消费后，其后间隔会漏掉
--    （如 “g\nsd” 在 “sdagdsa\ng” 被补后需要再一遍），因此循环
--    补到稳定为止（每遍都会收敛，空行已正确的行不会再匹配）。
repeat
    local before = out
    out = out:gsub("([^ \t\r\n][ \t]*)\n([ \t]*[^ \t\r\n])", "%1\n\n%2")
until out == before
if crlf then out = out:gsub("\n", "\r\n") end

if out ~= sel then
    editor:replace_selection(out)
    print("Adjusted blank-line spacing (" .. #out - #sel .. " chars net).")
else
    print("No change needed.")
end
