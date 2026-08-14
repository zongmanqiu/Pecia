-- ==Meta==
-- @name Unordered List
-- @name.zh-CN 无序列表
-- @date 20260812
-- ==/Meta==

-- 选中多行，行首加 "- " 无序列表标记；已有 "- " 的行切换为移除。
-- 无选区时插入 "- " 列表标记，光标置于其后。

local sel = editor:get_selection()
if sel == "" then
    editor:replace_selection("- ")
    return
end

-- 选区全为空白时视为普通内容，避免被“空行保持”分支跳过
local allBlank = (sel:match("^[ \t\r\n]*$") ~= nil)

local crlf = sel:find("\r\n") ~= nil
local changed = 0
local out = {}
for line in (sel:gsub("\r\n", "\n") .. "\n"):gmatch("(.-)\n") do
    local indent, rest = line:match("^([ \t]*)-[ \t]+(.*)$")
    local outLine = line
    if indent then
        outLine = indent .. rest          -- 已有标记：移除（切换）
        changed = changed + 1
    elseif line:match("^[ \t]*$") and not allBlank then
        -- 文档中的空行保持不变
    else
        outLine = line:gsub("^([ \t]*)", "%1- ")   -- 缩进后加标记（支持多级嵌套）
        changed = changed + 1
    end
    table.insert(out, outLine)
end
local result = table.concat(out, "\n")
if crlf then result = result:gsub("\n", "\r\n") end

editor:replace_selection(result)
print("Toggled list marker on " .. changed .. " line(s).")
