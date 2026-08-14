-- ==Meta==
-- @name Punctuation to EN
-- @name.zh-CN 标点英文化
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Convert Chinese/full-width punctuation to English ASCII: quotes,
-- brackets, comma/period/semicolon/colon/question/exclamation, the
-- ellipsis and the full-width space.
--
-- Book-title marks《》have no ASCII equivalent and are kept as-is.
-- The em dash — is already used in English and is kept as-is.

local MAP = {
    ["“"] = '"', ["”"] = '"',
    ["‘"] = "'", ["’"] = "'",
    ["（"] = "(", ["）"] = ")",
    ["【"] = "[", ["】"] = "]",
    ["。"] = ".", ["，"] = ",", ["；"] = ";", ["："] = ":",
    ["？"] = "?", ["！"] = "!",
    ["…"] = "...",
    ["　"] = " ",  -- full-width space
}

local sel = editor:get_selection()
if #sel == 0 then
    print("No selection - select the text to convert first.")
    return
end

local out = {}
local i = 1
while i <= #sel do
    local b = sel:byte(i)
    local clen = 1
    if b >= 0xF0 then clen = 4
    elseif b >= 0xE0 then clen = 3
    elseif b >= 0xC0 then clen = 2
    end
    local c = sel:sub(i, i + clen - 1)
    out[#out + 1] = MAP[c] or c
    i = i + clen
end
editor:replace_selection(table.concat(out, ""))
