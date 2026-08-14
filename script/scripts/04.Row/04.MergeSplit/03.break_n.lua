-- ==Meta==
-- @name Split Every x Chars
-- @name.zh-CN 按 x 字符拆分
-- @author 邱宗满
-- @date 20260810
--!param n=10: characters per line
--!pui.title = Break Lines by Chars
--!pui.title.zh-CN = 按字符数断行
--!pui.n = Characters per line
--!pui.n.zh-CN = 每行字符数
-- ==/Meta==

-- Break every line after every N characters (UTF-8 aware - Chinese
-- characters are never split). The last partial segment stays as-is.

local sel = editor:get_selection()
if #sel == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the text to process first.")
    end
    return
end

local n = tonumber(params.n) or 10
if n < 1 then n = 1 end

-- Split one line into chunks of at most `n` characters.
local function chunksOf(s, n)
    local out = {}
    local i = 1
    local count = 0
    local start = 1
    while i <= #s do
        local b = s:byte(i)
        local clen = 1
        if b >= 0xF0 then clen = 4
        elseif b >= 0xE0 then clen = 3
        elseif b >= 0xC0 then clen = 2
        end
        count = count + 1
        if count == n then
            out[#out + 1] = s:sub(start, i + clen - 1)
            start = i + clen
            count = 0
        end
        i = i + clen
    end
    if count > 0 then
        out[#out + 1] = s:sub(start)
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
    local chunks = chunksOf(line, n)
    out[#out + 1] = table.concat(chunks, "\n") .. eol
    if not nl then break end
    start = nl + #eol
end
editor:replace_selection(table.concat(out, ""))
