-- ==Meta==
-- @name Left: x chars
-- @name.zh-CN 左侧: x 字符
-- @author 邱宗满
-- @date 20260810
--!param count=10: number of characters to keep
--!pui.title = Take Left Chars
--!pui.title.zh-CN = 截取左侧字符
--!pui.count = Characters
--!pui.count.zh-CN = 字符数
-- ==/Meta==

-- Keep only the first N characters of every line (UTF-8 aware).

local sel = editor:get_selection()
if #sel == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the lines to process first.")
    end
    return
end

local count = tonumber(params.count) or 10
if count < 0 then count = 0 end

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
    local n = utf8.len(line)
    if count < n then
        local e = utf8.offset(line, count + 1)
        line = line:sub(1, e - 1)
    end
    out[#out + 1] = line .. eol
    if not nl then break end
    start = nl + #eol
end
editor:replace_selection(table.concat(out, ""))
