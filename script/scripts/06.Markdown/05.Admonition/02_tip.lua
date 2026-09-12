-- ==Meta==
-- @name Tip
-- @name.zh-CN 提示
-- @name.zh-TW 提示
-- @name.ja ヒント
-- @name.de Tipp
-- @name.fr Astuce
-- @name.es Consejo
-- @name.ru Совет
-- @name.ko 팁
-- @name.pt-BR Dica
-- @name.it Suggerimento
-- @name.ar نصيحة
-- @name.hi सुझाव
-- @name.id Tips
-- @name.tr İpucu
-- @name.vi Mẹo
-- @date 20260813
-- ==/Meta==

-- 插入 tip 提醒块（绿色）。无选区：插入模板，光标置于内容处；
-- 有选区：把选区包进提醒块（每行自动加 > 前缀）。

if input == "" then
    insert("> [!TIP]\n> ")
    return
end
local body = input:gsub("\r\n", "\n"):gsub("\n", "\n> ")
return "> [!TIP]\n> " .. body
