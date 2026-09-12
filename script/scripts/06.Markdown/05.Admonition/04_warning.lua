-- ==Meta==
-- @name Warning
-- @name.zh-CN 警告
-- @name.zh-TW 警告
-- @name.ja 警告
-- @name.de Warnung
-- @name.fr Avertissement
-- @name.es Advertencia
-- @name.ru Предупреждение
-- @name.ko 경고
-- @name.pt-BR Aviso
-- @name.it Avvertenza
-- @name.ar تحذير
-- @name.hi चेतावनी
-- @name.id Peringatan
-- @name.tr Uyarı
-- @name.vi Cảnh báo
-- @date 20260813
-- ==/Meta==

-- 插入 warning 提醒块（黄色）。无选区：插入模板，光标置于内容处；
-- 有选区：把选区包进提醒块（每行自动加 > 前缀）。

if input == "" then
    insert("> [!WARNING]\n> ")
    return
end
local body = input:gsub("\r\n", "\n"):gsub("\n", "\n> ")
return "> [!WARNING]\n> " .. body
