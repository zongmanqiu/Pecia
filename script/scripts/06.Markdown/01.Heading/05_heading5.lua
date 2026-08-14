-- ==Meta==
-- @name ##### Heading 5
-- @name.zh-CN ##### 标题5
-- @date 20260812
-- ==/Meta==

-- 选中多行，行首添加 5 级标题标记（已有标题标记会先移除）。

local sel = editor:get_selection()
if sel == "" then
    editor:replace_selection("##### ")   -- 无选区：插入标题标记
    return
end

local crlf = sel:find("\r\n") ~= nil
local out = {}
for line in (sel:gsub("\r\n", "\n") .. "\n"):gmatch("(.-)\n") do
    local hashes = line:match("^[ \t]*(#+)")
    if hashes and #hashes == 5 then
        table.insert(out, line)          -- 已是标题5：保持
    else
        local outLine = line:gsub("^[ \t]*#+[ \t]*", "")
        table.insert(out, "##### " .. outLine)
    end
end
local result = table.concat(out, "\n")
if crlf then result = result:gsub("\n", "\r\n") end

editor:replace_selection(result)
print("Heading 5 applied to " .. #out .. " line(s).")