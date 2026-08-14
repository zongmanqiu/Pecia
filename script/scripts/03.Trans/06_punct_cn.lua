-- ==Meta==
-- @name Punctuation to CN
-- @name.zh-CN 标点中文化
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Convert English/ASCII punctuation to Chinese full-width forms:
-- quotes (alternating open/close), brackets, comma/period/semicolon/
-- colon/question/exclamation, and "..." to the ellipsis.
--
-- Protected from conversion:
--   * everything inside a URL (http://... until whitespace)
--   * periods inside numbers/domain names (3.14, example.com)
--   * commas inside numbers (1,000)
--   * apostrophes inside words (don't)
-- Known limitation: abbreviations like "e.g." are not detected, so
-- their trailing period becomes a Chinese period.

local sel = editor:get_selection()
if #sel == 0 then
    print("No selection - select the text to convert first.")
    return
end

local out = {}
local dqOpen, sqOpen = true, true
local inUrl = false
local i = 1
local n = #sel
while i <= n do
    local b = sel:byte(i)
    local clen = 1
    if b >= 0xF0 then clen = 4
    elseif b >= 0xE0 then clen = 3
    elseif b >= 0xC0 then clen = 2
    end
    local c = sel:sub(i, i + clen - 1)
    local prev = sel:sub(i - 1, i - 1)
    local next = sel:sub(i + clen, i + clen)

    if sel:sub(i, i + 2) == "://" then inUrl = true end
    if c:match("%s") then inUrl = false end

    if inUrl then
        out[#out + 1] = c   -- leave URLs untouched
    elseif c == '"' then
        out[#out + 1] = dqOpen and "“" or "”"
        dqOpen = not dqOpen
    elseif c == "'" then
        if prev:match("%a") and next:match("%a") then
            out[#out + 1] = "'"   -- apostrophe inside a word (don't)
        else
            out[#out + 1] = sqOpen and "‘" or "’"
            sqOpen = not sqOpen
        end
    elseif c == "." then
        -- "..." must be checked before the single-dot case.
        if sel:sub(i, i + 2) == "..." then
            out[#out + 1] = "…"   -- ellipsis
            i = i + 2                 -- + clen below = 3 total
        elseif prev:match("%w") and next:match("%w") then
            out[#out + 1] = "."   -- inside numbers / domain names
        else
            out[#out + 1] = "。"
        end
    elseif c == "," then
        if prev:match("%d") and next:match("%d") then
            out[#out + 1] = ","   -- thousands separator (1,000)
        else
            out[#out + 1] = "，"
        end
    elseif c == ":" then
        if next == "/" then
            out[#out + 1] = ":"   -- http:// and friends
        else
            out[#out + 1] = "："
        end
    elseif c == "(" then out[#out + 1] = "（"
    elseif c == ")" then out[#out + 1] = "）"
    elseif c == "[" then out[#out + 1] = "【"
    elseif c == "]" then out[#out + 1] = "】"
    elseif c == ";" then out[#out + 1] = "；"
    elseif c == "?" then out[#out + 1] = "？"
    elseif c == "!" then out[#out + 1] = "！"
    else
        out[#out + 1] = c
    end
    i = i + clen
end
editor:replace_selection(table.concat(out, ""))
