-- ==Meta==
-- @name Link
-- @name.zh-CN 链接
-- @name.zh-TW 鏈接
-- @name.ja リンク
-- @name.de Link
-- @name.fr Lien
-- @name.es Enlace
-- @name.ru Ссылка
-- @name.ko 링크
-- @name.pt-BR Link
-- @name.it Collegamento
-- @name.ar رابط
-- @name.hi लिंक
-- @name.id Tautan
-- @name.tr Bağlantı
-- @name.vi Liên kết
-- @date 20260812
-- ==/Meta==

-- 选中文本转为链接 [选中文本]()，光标置于括号内等待输入 URL；
-- 无选区时插入 []()，光标置于方括号内等待输入显示文本。

if input == "" then
    insert("[]()", -3)
    return
end
insert("[" .. input .. "]()", -1)
