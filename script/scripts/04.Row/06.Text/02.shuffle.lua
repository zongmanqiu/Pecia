-- ==Meta==
-- @name Shuffle
-- @name.zh-CN 乱序
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Randomize the characters of every line (UTF-8 aware - a Chinese
-- character stays one unit). Line order is preserved.

local sel = editor:get_selection()
if #sel == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the lines to process first.")
    end
    return
end

local function charsOf(s)
    local out = {}
    local i = 1
    while i <= #s do
        local b = s:byte(i)
        local clen = 1
        if b >= 0xF0 then clen = 4
        elseif b >= 0xE0 then clen = 3
        elseif b >= 0xC0 then clen = 2
        end
        out[#out + 1] = s:sub(i, i + clen - 1)
        i = i + clen
    end
    return out
end

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
    local ch = charsOf(line)
    for i = #ch, 2, -1 do
        local j = math.random(i)
        ch[i], ch[j] = ch[j], ch[i]
    end
    out[#out + 1] = table.concat(ch, "") .. eol
    if not nl then break end
    start = nl + #eol
end
editor:replace_selection(table.concat(out, ""))
