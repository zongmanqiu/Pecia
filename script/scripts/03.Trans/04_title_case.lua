-- ==Meta==
-- @name Title Case
-- @name.zh-CN 单词首字母大写（介词除外）
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Capitalize the first letter of every ASCII word, except small words
-- (articles, conjunctions, prepositions) which stay lowercase - unless
-- they start a sentence, in which case they are capitalized too.
--
-- Small-word list covers common English articles/conjunctions/
-- prepositions. Words with digits or punctuation inside (e.g. "e-mail")
-- are not recognized as words and stay untouched.

local SMALL = {}
for _, w in ipairs({
    "a", "an", "the",
    "and", "or", "but", "nor", "for", "yet", "so",
    "of", "in", "on", "at", "to", "from", "by", "with", "without",
    "about", "above", "across", "after", "against", "along", "among",
    "around", "as", "before", "behind", "below", "beneath", "beside",
    "between", "beyond", "during", "except", "into", "like", "near",
    "off", "over", "per", "since", "than", "through", "till", "toward",
    "under", "until", "up", "upon", "via", "within",
}) do
    SMALL[w] = true
end

local sel = editor:get_selection()
if #sel == 0 then
    print("No selection - select the text to convert first.")
    return
end

local out = {}
local sentStart = true
local i = 1
while i <= #sel do
    local b = sel:byte(i)
    local clen = 1
    if b >= 0xF0 then clen = 4
    elseif b >= 0xE0 then clen = 3
    elseif b >= 0xC0 then clen = 2
    end
    local c = sel:sub(i, i + clen - 1)

    -- ASCII word (letters, optionally with an apostrophe inside).
    local w = sel:match("^[A-Za-z]+'?[A-Za-z]*", i)
    if w then
        local lw = w:lower()
        local word
        if sentStart or not SMALL[lw] then
            word = w:sub(1, 1):upper() .. w:sub(2):lower()
        else
            word = lw
        end
        out[#out + 1] = word
        sentStart = false
        i = i + #w
    else
        out[#out + 1] = c
        if c:match("[%.%?!%。！？]") then sentStart = true end
        i = i + clen
    end
end
editor:replace_selection(table.concat(out, ""))
