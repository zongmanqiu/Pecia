-- ==Meta==
-- @name To Halfwidth
-- @name.zh-CN 转半角
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Convert full-width ASCII look-alikes back to half-width: Ａ -> A,
-- １ -> 1, ＂ -> ", full-width space -> space. Only the U+FF01..U+FF5E
-- range and U+3000 are converted; Chinese characters are untouched.

local sel = editor:get_selection()
if #sel == 0 then
    print("No selection - select the text to convert first.")
    return
end

local out = {}
local i = 1
local n = #sel
while i <= n do
    local b = sel:byte(i)
    local clen = 1
    if b >= 0xF0 then clen = 4
    elseif b >= 0xE0 then clen = 3
    elseif b >= 0xC0 then clen = 2
    end

    if clen == 3 then
        local b2 = sel:byte(i + 1)
        local b3 = sel:byte(i + 2)
        local cp = ((b & 0x0F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F)
        if cp == 0x3000 then
            out[#out + 1] = " "                 -- full-width space
        elseif cp >= 0xFF01 and cp <= 0xFF5E then
            out[#out + 1] = string.char(cp - 0xFF00 + 0x20)
        else
            out[#out + 1] = sel:sub(i, i + clen - 1)
        end
    else
        out[#out + 1] = sel:sub(i, i + clen - 1)
    end
    i = i + clen
end
editor:replace_selection(table.concat(out, ""))
