-- ==Meta==
-- @name To Halfwidth
-- @name.zh-CN 转半角
-- @name.zh-TW 轉半角
-- @name.ja 半角に変換
-- @name.de In Halbbreite
-- @name.fr En demi-largeur
-- @name.es A ancho medio
-- @name.ru В половинную ширину
-- @name.ko 반각으로 변환
-- @name.pt-BR Para metade da largura
-- @name.it A mezza larghezza
-- @name.ar تحويل إلى نصف عرض
-- @name.hi आधी चौड़ाई में
-- @name.id Ke Lebar Setengah
-- @name.tr Yarı Genişliğe
-- @name.vi Chuyển thành bán凫
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Convert full-width ASCII look-alikes back to half-width: Ａ -> A,
-- １ -> 1, ＂ -> ", full-width space -> space. Only the U+FF01..U+FF5E
-- range and U+3000 are converted; Chinese characters are untouched.

if #input == 0 then
    print("No selection - select the text to convert first.")
    return
end

local out = {}
local i = 1
local n = #input
while i <= n do
    local b = input:byte(i)
    local clen = 1
    if b >= 0xF0 then clen = 4
    elseif b >= 0xE0 then clen = 3
    elseif b >= 0xC0 then clen = 2
    end

    if clen == 3 then
        local b2 = input:byte(i + 1)
        local b3 = input:byte(i + 2)
        local cp = ((b & 0x0F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F)
        if cp == 0x3000 then
            out[#out + 1] = " "                 -- full-width space
        elseif cp >= 0xFF01 and cp <= 0xFF5E then
            out[#out + 1] = string.char(cp - 0xFF00 + 0x20)
        else
            out[#out + 1] = input:sub(i, i + clen - 1)
        end
    else
        out[#out + 1] = input:sub(i, i + clen - 1)
    end
    i = i + clen
end
return table.concat(out, "")
