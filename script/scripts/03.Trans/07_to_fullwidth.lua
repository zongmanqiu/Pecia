-- ==Meta==
-- @name To Fullwidth
-- @name.zh-CN 转全角
-- @name.zh-TW 轉全角
-- @name.ja 全角に変換
-- @name.de In Vollbreite
-- @name.fr En pleine largeur
-- @name.es A ancho completo
-- @name.ru В полную ширину
-- @name.ko 전각으로 변환
-- @name.pt-BR Para largura total
-- @name.it A larghezza piena
-- @name.ar تحويل إلى عرض كامل
-- @name.hi पूर्ण चौड़ाई में
-- @name.id Ke Lebar Penuh
-- @name.tr Tam Genişliğe
-- @name.vi Chuyển thành toàn凫
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Convert half-width ASCII (letters, digits, symbols) and the space to
-- full-width forms: A -> Ａ, 1 -> １, " -> ＂, space -> full-width space.
-- Chinese characters are untouched (already full-width by nature).
-- Letters are converted to their full-width forms, NOT to their
-- Chinese look-alikes.

if #input == 0 then
    print("No selection - select the text to convert first.")
    return
end

local out = {}
-- Encode a Unicode code point (U+FF01..U+FF5E, U+3000) as UTF-8 bytes.
local function utf8enc(cp)
    return string.char(0xE0 | (cp >> 12),
                       0x80 | ((cp >> 6) & 0x3F),
                       0x80 | (cp & 0x3F))
end
local i = 1
local n = #input
while i <= n do
    local b = input:byte(i)
    if b == 0x20 then
        out[#out + 1] = utf8enc(0x3000)        -- space -> full-width space
        i = i + 1
    elseif b >= 0x21 and b <= 0x7E then
        out[#out + 1] = utf8enc(0xFF00 + (b - 0x20))   -- ! -> ！, A -> Ａ
        i = i + 1
    else
        -- multi-byte (Chinese etc.): copy the whole character verbatim
        local clen = 1
        if b >= 0xF0 then clen = 4
        elseif b >= 0xE0 then clen = 3
        elseif b >= 0xC0 then clen = 2
        end
        out[#out + 1] = input:sub(i, i + clen - 1)
        i = i + clen
    end
end
return table.concat(out, "")
