-- ==Meta==
-- @name Pad Numbers
-- @name.zh-CN 数字补0
-- @name.zh-TW 數字補0
-- @name.ja ゼロ埋め
-- @name.de Zahlen auffüllen
-- @name.fr Remplir les nombres
-- @name.es Rellenar números
-- @name.ru Дополнение нулями
-- @name.ko 숫자 채우기
-- @name.pt-BR Preencher números
-- @name.it Riempimento numeri
-- @name.ar ملء الأرقام
-- @name.hi संख्याएँ भरें
-- @name.id Tambah Nol Angka
-- @name.tr Sayı Doldurma
-- @name.vi Bổ sung số
-- @author 邱宗满
-- @date 20260810
--!param where=before|after
--!param width=4
--!pui.title = Pad Numbers
--!pui.title.zh-CN = 数字补0
--!pui.title.zh-TW = 標題
--!pui.title.ja = タイトル
--!pui.title.de = Titel
--!pui.title.fr = Titre
--!pui.title.es = Título
--!pui.title.ru = Заголовок
--!pui.title.ko = 제목
--!pui.title.pt-BR = Título
--!pui.title.it = Titolo
--!pui.title.ar = العنوان
--!pui.title.hi = शीर्षक
--!pui.title.id = Judul
--!pui.title.tr = Başlık
--!pui.title.vi = Tiêu đề
--!pui.where = Where to pad
--!pui.where.zh-CN = 补0位置
--!pui.where.zh-TW = 補0位置
--!pui.where.ja = ゼロ埋め位置
--!pui.where.de = Position
--!pui.where.fr = Position
--!pui.where.es = Posición
--!pui.where.ru = Позиция
--!pui.where.ko = 채우기 위치
--!pui.where.pt-BR = Posição
--!pui.where.it = Posizione
--!pui.where.ar = الموضع
--!pui.where.hi = स्थिति
--!pui.where.id = Posisi
--!pui.where.tr = Konum
--!pui.where.vi = Vị trí
--!pui.where.before = Integer part
--!pui.where.before.zh-CN = 整数部分
--!pui.where.before.zh-TW = 整數部分
--!pui.where.before.ja = 整数部分
--!pui.where.before.de = Ganzzahlteil
--!pui.where.before.fr = Partie entière
--!pui.where.before.es = Parte entera
--!pui.where.before.ru = Целая часть
--!pui.where.before.ko = 정수 부분
--!pui.where.before.pt-BR = Parte inteira
--!pui.where.before.it = Parte intera
--!pui.where.before.ar = الجزء الصحيح
--!pui.where.before.hi = पूर्णांक भाग
--!pui.where.before.id = Bagian Bulat
--!pui.where.before.tr = Tam Sayı Kısmı
--!pui.where.before.vi = Phần nguyên
--!pui.where.after = Decimal places
--!pui.where.after.zh-CN = 小数部分
--!pui.where.after.zh-TW = 小數部分
--!pui.where.after.ja = 小数部分
--!pui.where.after.de = Dezimalteil
--!pui.where.after.fr = Partie décimale
--!pui.where.after.es = Parte decimal
--!pui.where.after.ru = Дробная часть
--!pui.where.after.ko = 소수 부분
--!pui.where.after.pt-BR = Parte decimal
--!pui.where.after.it = Parte decimale
--!pui.where.after.ar = الجزء العشري
--!pui.where.after.hi = दशमलव भाग
--!pui.where.after.id = Bagian Desimal
--!pui.where.after.tr = Ondalık Kısım
--!pui.where.after.vi = Phần thập phân
--!pui.width = Target digits
--!pui.width.zh-CN = 目标位数
--!pui.width.zh-TW = 目標位數
--!pui.width.ja = 桁数
--!pui.width.de = Zielziffern
--!pui.width.fr = Chiffres cibles
--!pui.width.es = Dígitos objetivo
--!pui.width.ru = Целевые разряды
--!pui.width.ko = 목표 자릿수
--!pui.width.pt-BR = Dígitos alvo
--!pui.width.it = Cifre obiettivo
--!pui.width.ar = الأرقام المستهدفة
--!pui.width.hi = लक्ष्य अंक
--!pui.width.id = Digit Target
--!pui.width.tr = Hane Sayısı
--!pui.width.vi = Số chữ số mục tiêu
-- ==/Meta==

-- Zero-pad numbers in the selection.
--
--   where=before: every integer part is left-padded to `width` digits:
--     "1345 and 36" -> "1345 and 0036" (with width=4). Negative numbers
--     keep their sign: -36 -> -0036. Decimal parts are left untouched,
--     so 13.4 -> 0013.4 (integer part padded).
--
--   where=after: decimal places are right-padded to `width` digits:
--     13.4 -> 13.400 (width=3). Numbers without a decimal point are
--     left untouched.
--
-- Known limitation: multi-dot tokens like "1.2.3" (version numbers) are
-- matched as "1.2" then "3", so before-mode pads each part separately.

if #input == 0 then
    print("No selection - select the text to convert first.")
    return
end

local where = params.where or "before"
local width = tonumber(params.width)
if where ~= "before" and where ~= "after" then
    print("Invalid where value '" .. where .. "' - use before or after.")
    return
end
if not width or width < 1 or width > 20 then
    print("Invalid width '" .. tostring(params.width) .. "' - use a number between 1 and 20.")
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
    local c = input:sub(i, i + clen - 1)

    local consumed = false
    if c:match("%d") then
        if where == "before" then
            -- Match the whole number incl. an optional decimal part, but
            -- pad only the integer part: 164.1 (width=4) -> 0164.1.
            local num = input:match("^%d+%.?%d*", i)
            if num then
                local intPart, decPart = num:match("^(%d+)%.?(%d*)$")
                local padded = string.rep("0", width - #intPart) .. intPart
                if decPart and #decPart > 0 then
                    out[#out + 1] = padded .. "." .. decPart
                else
                    out[#out + 1] = padded
                end
                i = i + #num
                consumed = true
            end
        else
            local num = input:match("^%d+%.%d+", i)
            if num then
                local intPart, decPart = num:match("^(%d+)%.(%d+)$")
                if #decPart < width then
                    out[#out + 1] = intPart .. "." .. decPart
                        .. string.rep("0", width - #decPart)
                else
                    out[#out + 1] = num
                end
                i = i + #num
                consumed = true
            end
        end
    end
    if not consumed then
        out[#out + 1] = c
        i = i + clen
    end
end
return table.concat(out, "")
