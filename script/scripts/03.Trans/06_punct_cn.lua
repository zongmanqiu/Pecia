-- ==Meta==
-- @name Punctuation to CN
-- @name.zh-CN 标点中文化
-- @name.zh-TW 標點中文化
-- @name.ja 中国語句読点に変換
-- @name.de Satzzeichen zu CN
-- @name.fr Ponctuation CN
-- @name.es Puntuación CN
-- @name.ru Пунктуация CN
-- @name.ko 중국어 문장부호로 변환
-- @name.pt-BR Pontuação para CN
-- @name.it Punteggiatura CN
-- @name.ar ترقيم CN
-- @name.hi विराम चिह्न CN में
-- @name.id Tanda Baca CN
-- @name.tr CN Noktalama
-- @name.vi Dấu CN
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

if #input == 0 then
    print("No selection - select the text to convert first.")
    return
end

local out = {}
local dqOpen, sqOpen = true, true
local inUrl = false
local i = 1
local n = #input
while i <= n do
    local b = input:byte(i)
    local clen = 1
    if b >= 0xF0 then clen = 4
    elseif b >= 0xE0 then clen = 3
    elseif b >= 0xC0 then clen = 2
    end
    local c = input:sub(i, i + clen - 1)
    local prev = input:sub(i - 1, i - 1)
    local next = input:sub(i + clen, i + clen)

    if input:sub(i, i + 2) == "://" then inUrl = true end
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
        if input:sub(i, i + 2) == "..." then
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
return table.concat(out, "")
