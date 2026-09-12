-- ==Meta==
-- @name Add Thousands Separator
-- @name.zh-CN 千分位逗号
-- @name.zh-TW 千分位逗號
-- @name.ja 桁区切り追加
-- @name.de Tausendertrennzeichen
-- @name.fr Séparateur de milliers
-- @name.es Separador de miles
-- @name.ru Разделитель тысяч
-- @name.ko 천의 자리 구분 기호
-- @name.pt-BR Separador de milhar
-- @name.it Separatore migliaia
-- @name.ar فاصل الآلاف
-- @name.hi हज़ार विभाजक
-- @name.id Pemisah Ribuan
-- @name.tr Binlik Ayraç
-- @name.vi Dấu phân cách hàng nghìn
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Add thousands separators to integer parts in the selection:
--   1234567 -> 1,234,567   12.5 -> 12.5   1000 -> 1,000
-- Already-separated numbers (1,234) are normalized, so re-running the
-- script is safe. Numbers shorter than 4 digits are left as-is.
--
-- Known limitation: sequences like "1234567890123" (more than one
-- group) are handled fine, but a single run of 12+ digits is one match.

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
    local c = input:sub(i, i + clen - 1)

    local consumed = false
    if c:match("%d") then
        -- digits possibly already separated by commas (1,234), plus an
        -- optional decimal part which is NEVER reformatted (1,234.5678).
        local num = input:match("^%d[%d,]*%.?%d*", i)
        if num then
            local intPart, decPart = num:match("^(%d[%d,]*)(%.?%d*)$")
            local digits = intPart:gsub(",", "")
            local formatted
            if #digits > 3 then
                local groups = {}
                while #digits > 3 do
                    groups[#groups + 1] = digits:sub(-3)
                    digits = digits:sub(1, -4)
                end
                groups[#groups + 1] = digits
                local rev = {}
                for k = #groups, 1, -1 do rev[#rev + 1] = groups[k] end
                formatted = table.concat(rev, ",")
            else
                formatted = digits
            end
            out[#out + 1] = formatted .. decPart
            i = i + #num
            consumed = true
        end
    end
    if not consumed then
        out[#out + 1] = c
        i = i + clen
    end
end
return table.concat(out, "")
