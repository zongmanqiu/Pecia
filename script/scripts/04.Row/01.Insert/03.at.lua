-- ==Meta==
-- @name After Char x
-- @name.zh-CN 第 x 字符后
-- @author 邱宗满
-- @date 20260810
--!param pos=1: character index (1 = after the first character)
--!param text=: text to insert
--!pui.title = Insert at Position
--!pui.title.zh-CN = 第 x 字符后插入
--!pui.pos = Char index
--!pui.pos.zh-CN = 字符位置
--!pui.text = Text
--!pui.text.zh-CN = 插入内容
-- ==/Meta==

-- Insert text after the Nth character (UTF-8 aware - counts characters,
-- not bytes; 0 = start of line, larger than the line = end of line).

local sel = editor:get_selection()
if #sel == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the lines to process first.")
    end
    return
end

local pos = tonumber(params.pos) or 1
if pos < 0 then pos = 0 end
local text = params.text or ""

local function charEndByte(line, count)
    -- Byte position right after the first `count` characters.
    local n = utf8.len(line)
    if count >= n then return #line + 1 end
    return utf8.offset(line, count + 1)
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
    local bpos = charEndByte(line, pos)
    out[#out + 1] = line:sub(1, bpos - 1) .. text .. line:sub(bpos) .. eol
    if not nl then break end
    start = nl + #eol
end
editor:replace_selection(table.concat(out, ""))
