-- ==Meta==
-- @name Duplicated Lines
-- @name.zh-CN 重复行
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Remove duplicate lines from the selection: the FIRST occurrence of
-- every line is kept, later duplicates are dropped (order preserved).

local sel = editor:get_selection()
if #sel == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the lines to process first.")
    end
    return
end

local seen = {}
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
    if not seen[line] then
        seen[line] = true
        out[#out + 1] = line .. eol
    end
    if not nl then break end
    start = nl + #eol
end
editor:replace_selection(table.concat(out, ""))
