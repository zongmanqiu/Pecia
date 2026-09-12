-- ==Meta==
-- @name Note
-- @name.zh-CN 备注
-- @name.zh-TW 備註
-- @name.ja 注意
-- @name.de Hinweis
-- @name.fr Note
-- @name.es Nota
-- @name.ru Примечание
-- @name.ko 참고
-- @name.pt-BR Nota
-- @name.it Nota
-- @name.ar ملاحظة
-- @name.hi नोट
-- @name.id Catatan
-- @name.tr Not
-- @name.vi Ghi chú
-- @date 20260813
-- ==/Meta==

-- 插入 note 提醒块（蓝色）。无选区：插入模板，光标置于内容处；
-- 有选区：把选区包进提醒块（每行自动加 > 前缀）。

if input == "" then
    insert("> [!NOTE]\n> ")
    return
end
local body = input:gsub("\r\n", "\n"):gsub("\n", "\n> ")
return "> [!NOTE]\n> " .. body
