-- ==Meta==
-- @name Number Headings
-- @name.zh-CN 标题编号
-- @date 20260812
-- ==/Meta==

-- 选中多行，为各级标题添加数字编号：1. / 1.1 / 1.1.1（按层级递增，
-- 子级在父级变化时重置）。已有编号会被替换；非标题行保持不变。

local sel = editor:get_selection()
if sel == "" then
    print("No selection - select the heading lines first.")
    return
end

local crlf = sel:find("\r\n") ~= nil
local counts = {}
local out = {}
for line in (sel:gsub("\r\n", "\n") .. "\n"):gmatch("(.-)\n") do
    local level, text = line:match("^(#+)[ \t]*(.*)$")
    if level then
        local lv = #level
        counts[lv] = (counts[lv] or 0) + 1
        for i = lv + 1, 6 do counts[i] = nil end
        text = text:gsub("^%d+%.%d*[ \t]*", "")   -- 移除旧编号
        local num = {}
        for i = 1, lv do table.insert(num, counts[i] or 0) end
        table.insert(out, string.rep("#", lv) .. " " ..
                           table.concat(num, ".") .. " " .. text)
    else
        table.insert(out, line)
    end
end
local result = table.concat(out, "\n")
if crlf then result = result:gsub("\n", "\r\n") end

editor:replace_selection(result)
print("Headings numbered.")
