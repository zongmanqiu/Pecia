-- ==Meta==
-- @name Punctuation to EN
-- @name.zh-CN 标点英文化
-- @name.zh-TW 標點英文化
-- @name.ja 英語句読点に変換
-- @name.de Satzzeichen zu EN
-- @name.fr Ponctuation EN
-- @name.es Puntuación EN
-- @name.ru Пунктуация EN
-- @name.ko 영어 문장부호로 변환
-- @name.pt-BR Pontuação para EN
-- @name.it Punteggiatura EN
-- @name.ar ترقيم EN
-- @name.hi विराम चिह्न EN में
-- @name.id Tanda Baca EN
-- @name.tr EN Noktalama
-- @name.vi Dấu EN
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

if #input == 0 then
    print("No selection - select the text to convert first.")
    return
end

local out = {}
local i = 1
while i <= #input do
    local b = input:byte(i)
    local clen = 1
    if b >= 0xF0 then clen = 4
    elseif b >= 0xE0 then clen = 3
    elseif b >= 0xC0 then clen = 2
    end
    local c = input:sub(i, i + clen - 1)
    out[#out + 1] = MAP[c] or c
    i = i + clen
end
return table.concat(out, "")
