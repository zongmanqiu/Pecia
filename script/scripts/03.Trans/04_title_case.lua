-- ==Meta==
-- @name Title Case
-- @name.zh-CN 单词首字母大写（介词除外）
-- @name.zh-TW 單詞首字母大寫（介詞除外）
-- @name.ja 見出し風大文字
-- @name.de Titelfall groß
-- @name.fr Majuscule en début de mot
-- @name.es Mayúscula al inicio de palabra
-- @name.ru Заглавная в начале слова
-- @name.ko 제목 대소문자
-- @name.pt-BR Início de palavra maiúsculo
-- @name.it Maiuscola iniziale parola
-- @name.ar حرف كبير في بداية الكلمة
-- @name.hi शीर्षक शैली
-- @name.id Gaya Judul
-- @name.tr Başlık Büyük Harf
-- @name.vi Chữ hoa đầu từ
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

if #input == 0 then
    print("No selection - select the text to convert first.")
    return
end

local out = {}
local sentStart = true
local i = 1
while i <= #input do
    local b = input:byte(i)
    local clen = 1
    if b >= 0xF0 then clen = 4
    elseif b >= 0xE0 then clen = 3
    elseif b >= 0xC0 then clen = 2
    end
    local c = input:sub(i, i + clen - 1)

    -- ASCII word (letters, optionally with an apostrophe inside).
    local w = input:match("^[A-Za-z]+'?[A-Za-z]*", i)
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
return table.concat(out, "")
