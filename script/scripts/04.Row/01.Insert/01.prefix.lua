-- ==Meta==
-- @name Prefix
-- @name.zh-CN 前缀
-- @author 邱宗满
-- @date 20260810
--!param text=: text to insert
--!pui.title = Insert Prefix
--!pui.title.zh-CN = 插入前缀
--!pui.text = Text
--!pui.text.zh-CN = 插入内容
-- ==/Meta==

-- Prepend the given text to every line of the selection.

local sel = editor:get_selection()
if #sel == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the lines to process first.")
    end
    return
end

local text = params.text or ""

local out = {}
local start = 1
while start <= #sel do
    local nl = sel:find("[\r\n]", start)
    local line, eol = "", ""
    if nl then
        line = sel:sub(start, nl - 1)
        eol = sel:sub(nl, nl)
        if eol == "\r" and sel:sub(nl + 1, nl + 1) == "\n" then eol = "\r\n" end
    else
        line = sel:sub(start)
    end
    out[#out + 1] = text .. line .. eol
    if not nl then break end
    start = nl + #eol
end
editor:replace_selection(table.concat(out, ""))
