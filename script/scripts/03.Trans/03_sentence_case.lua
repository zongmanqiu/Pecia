-- ==Meta==
-- @name Sentence Case
-- @name.zh-CN 句子首字母大写
-- @name.zh-TW 句子首字母大寫
-- @name.ja 文頭大文字
-- @name.de Satzanfang groß
-- @name.fr Majuscule en début de phrase
-- @name.es Mayúscula al inicio de oración
-- @name.ru Заглавная в начале предложения
-- @name.ko 문장 첫 글자 대문자
-- @name.pt-BR Primeira maiúscula da frase
-- @name.it Maiuscola iniziale frase
-- @name.ar حرف كبير في بداية الجملة
-- @name.hi वाक्य के पहले अक्षर को बड़ा करें
-- @name.id Huruf Besar Awal Kalimat
-- @name.tr Cümle Başlığı Büyük
-- @name.vi Chữ hoa đầu câu
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

if #input == 0 then
    print("No selection - select the text to convert first.")
    return
end

local out = {}
local atStart = true   -- start of text counts as a sentence start
local i = 1
while i <= #input do
    local b = input:byte(i)
    local clen = 1
    if b >= 0xF0 then clen = 4
    elseif b >= 0xE0 then clen = 3
    elseif b >= 0xC0 then clen = 2
    end
    local c = input:sub(i, i + clen - 1)

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
return table.concat(out, "")
