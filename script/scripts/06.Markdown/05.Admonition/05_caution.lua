-- ==Meta==
-- @name Caution
-- @name.zh-CN 危险
-- @name.zh-TW 危險
-- @name.ja 危険
-- @name.de Vorsicht
-- @name.fr Attention
-- @name.es Precaución
-- @name.ru Осторожно
-- @name.ko 주의
-- @name.pt-BR Cuidado
-- @name.it Attenzione
-- @name.ar حذر
-- @name.hi सावधानी
-- @name.id Hati-hati
-- @name.tr Dikkat
-- @name.vi Thận trọng
-- @date 20260813
-- ==/Meta==

-- 插入 caution 提醒块（红色）。无选区：插入模板，光标置于内容处；
-- 有选区：把选区包进提醒块（每行自动加 > 前缀）。

if input == "" then
    insert("> [!CAUTION]\n> ")
    return
end
local body = input:gsub("\r\n", "\n"):gsub("\n", "\n> ")
return "> [!CAUTION]\n> " .. body
