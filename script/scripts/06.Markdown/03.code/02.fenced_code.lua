-- ==Meta==
-- @name Fenced Code Block
-- @name.zh-CN 围栏代码块
-- @date 20260812
-- ==/Meta==

-- 选中文本用 ``` 围栏包裹（首行/尾行加 ```，不指定语言）；
-- 无选区时插入空围栏，光标置于中间等待输入代码。

local sel = editor:get_selection()
if sel == "" then
    editor:replace_selection("```\n\n```")
    editor:set_cursor(editor:get_cursor() - 3)   -- 光标在围栏内的空行
    return
end

local crlf = sel:find("\r\n") ~= nil
local body = sel:gsub("\r\n", "\n")
if not body:match("^[ \t]*\n") then body = "\n" .. body end
if not body:match("\n[ \t]*$") then body = body .. "\n" end
local result = "```" .. body .. "```"
if crlf then result = result:gsub("\n", "\r\n") end

editor:replace_selection(result)
