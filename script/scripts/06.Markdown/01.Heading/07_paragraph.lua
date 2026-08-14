-- ==Meta==
-- @name Paragraph
-- @name.zh-CN 正文
-- @date 20260812
-- ==/Meta==

-- 选中多行，移除行首标题标记（# ），转为正文。非标题行保持不变。

local sel = editor:get_selection()
if sel == "" then
    print("No selection - select the heading lines first.")
    return
end

local crlf = sel:find("\r\n") ~= nil
local changed = 0
local out = {}
for line in (sel:gsub("\r\n", "\n") .. "\n"):gmatch("(.-)\n") do
    local content = line:match("^[ \t]*#+[ \t]*(.*)$")
    if content then
        table.insert(out, content)
        changed = changed + 1
    else
        table.insert(out, line)
    end
end
local result = table.concat(out, "\n")
if crlf then result = result:gsub("\n", "\r\n") end

if changed > 0 then
    editor:replace_selection(result)
    print("Converted " .. changed .. " heading(s) to paragraph.")
else
    print("No heading lines found.")
end
