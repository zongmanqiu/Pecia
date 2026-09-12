-- ==Meta==
-- @name Horizontal Rule
-- @name.zh-CN 水平分割线
-- @name.zh-TW 水平分割線
-- @name.ja 水平線
-- @name.de Horizontalregel
-- @name.fr Ligne horizontale
-- @name.es Línea horizontal
-- @name.ru Горизонтальная линия
-- @name.ko 수평 구분선
-- @name.pt-BR Linha horizontal
-- @name.it Linea orizzontale
-- @name.ar خط أفقي
-- @name.hi क्षैतिज रेखा
-- @name.id Garis Horizontal
-- @name.tr Yatay Çizgi
-- @name.vi Đường kẻ ngang
-- @date 20260812
-- ==/Meta==

-- 在光标处插入水平分割线 "---"（有选区时替换选区）。

if input == "" then
    insert("---")
else
    return "---"
end
