-- ==Meta==
-- @name Remove Thousands Separator
-- @name.zh-CN 去掉千分位逗号
-- @name.zh-TW 去掉千分位逗號
-- @name.ja 桁区切り削除
-- @name.de Tausendertrennzeichen entfernen
-- @name.fr Supprimer séparateur de milliers
-- @name.es Quitar separador de miles
-- @name.ru Убрать разделитель тысяч
-- @name.ko 천의 자리 구분 기호 제거
-- @name.pt-BR Remover separador de milhar
-- @name.it Rimuovi separatore migliaia
-- @name.ar إزالة فاصل الآلاف
-- @name.hi हज़ार विभाजक हटाएँ
-- @name.id Hapus Pemisah Ribuan
-- @name.tr Binlik Ayracı Kaldır
-- @name.vi Xóa dấu phân cách hàng nghìn
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Remove thousands separators from numbers in the selection:
--   1,234,567 -> 1234567   1,000 -> 1000
-- Only commas inside digit groups (digits,digits with >=3 trailing
-- digits) are removed, so ordinary text commas ("a, b") are untouched.
--
-- Known limitation: "1,2" (no 3-digit group) is left as-is.

if #input == 0 then
    print("No selection - select the text to convert first.")
    return
end

-- Match a digit run with comma separators: at least one comma followed
-- by a 3-digit group (or groups). Remove the commas inside the run.
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
        local num = input:match("^%d[%d,]*", i)
        if num and num:find(",", 1, true) then
            -- only strip commas that are between digit groups
            local cleaned = num:gsub(",", "")
            local digitCount = #cleaned
            local valid = num:match("^%d+,%d%d%d") ~= nil
            if valid then
                out[#out + 1] = cleaned
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
