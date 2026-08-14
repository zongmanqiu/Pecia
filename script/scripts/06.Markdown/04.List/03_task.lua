-- ==Meta==
-- @name Task List
-- @name.zh-CN 任务列表
-- @date 20260812
-- ==/Meta==

-- 选中多行：无任务标记的行加 "- [ ] "；已有 "- [ ]" 切换为 "- [x]"；
-- 已有 "- [x]" 切换回 "- [ ]"。无选区时插入 "- [ ] "，光标置于其后。

local sel = editor:get_selection()
if sel == "" then
    editor:replace_selection("- [ ] ")
    return
end

-- 选区全为空白时视为普通内容，避免被“空行保持”分支跳过
local allBlank = (sel:match("^[ \t\r\n]*$") ~= nil)

local crlf = sel:find("\r\n") ~= nil
local changed = 0
local out = {}
for line in (sel:gsub("\r\n", "\n") .. "\n"):gmatch("(.-)\n") do
    local outLine = line
    if line:match("^[ \t]*$") and not allBlank then
        table.insert(out, outLine)
    else
        local indent, box, rest = line:match("^([ \t]*)- %[([ xX])%][ \t]*(.*)$")
        if indent then
            if box == " " then
                outLine = indent .. "- [x] " .. rest
            else
                outLine = indent .. "- [ ] " .. rest
            end
            changed = changed + 1
        else
            outLine = "- [ ] " .. line
            changed = changed + 1
        end
    end
    table.insert(out, outLine)
end
local result = table.concat(out, "\n")
if crlf then result = result:gsub("\n", "\r\n") end

editor:replace_selection(result)
print("Toggled task marker on " .. changed .. " line(s).")
