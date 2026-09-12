-- ==Meta==
-- @name Important
-- @name.zh-CN 重要
-- @name.zh-TW 重要
-- @name.ja 重要
-- @name.de Wichtig
-- @name.fr Important
-- @name.es Importante
-- @name.ru Важно
-- @name.ko 중요
-- @name.pt-BR Importante
-- @name.it Importante
-- @name.ar مهم
-- @name.hi महत्वपूर्ण
-- @name.id Penting
-- @name.tr Önemli
-- @name.vi Quan trọng
-- @date 20260813
-- ==/Meta==

-- 插入 important 提醒块（紫色）。无选区：插入模板，光标置于内容处；
-- 有选区：把选区包进提醒块（每行自动加 > 前缀）。

if input == "" then
    insert("> [!IMPORTANT]\n> ")
    return
end
local body = input:gsub("\r\n", "\n"):gsub("\n", "\n> ")
return "> [!IMPORTANT]\n> " .. body
