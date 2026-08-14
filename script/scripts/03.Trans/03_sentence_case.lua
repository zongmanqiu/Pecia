-- ==Meta==
-- @name Sentence Case
-- @name.zh-CN 句子首字母大写
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Capitalize the first ASCII letter of every sentence. A sentence
-- boundary is '.', '!', '?' (or their full-width forms), optionally
-- followed by closing quotes/brackets and whitespace. All other
-- characters are left exactly as they are.
--
-- Known limitation: abbreviations like "e.g." or "Mr." are treated as
-- sentence ends, so the next word gets capitalized.

local sel = editor:get_selection()
if #sel == 0 then
    print("No selection - select the text to convert first.")
    return
end

local out = {}
local atStart = true   -- start of text counts as a sentence start
local i = 1
while i <= #sel do
    local b = sel:byte(i)
    local clen = 1
    if b >= 0xF0 then clen = 4
    elseif b >= 0xE0 then clen = 3
    elseif b >= 0xC0 then clen = 2
    end
    local c = sel:sub(i, i + clen - 1)

    if atStart and c:match("%a") then
        out[#out + 1] = c:upper()
        atStart = false
    else
        out[#out + 1] = c
    end
    if c:match("[%.%?!%。！？]") then
        atStart = true
    end
    i = i + clen
end
editor:replace_selection(table.concat(out, ""))
