-- ==Meta==
-- @name Image
-- @name.zh-CN 图片
-- @name.zh-TW 圖片
-- @name.ja 画像
-- @name.de Bild
-- @name.fr Image
-- @name.es Imagen
-- @name.ru Изображение
-- @name.ko 이미지
-- @name.pt-BR Imagem
-- @name.it Immagine
-- @name.ar صورة
-- @name.hi चित्र
-- @name.id Gambar
-- @name.tr Görsel
-- @name.vi Hình ảnh
-- @date 20260812
-- ==/Meta==

-- 选中文本转为图片 ![选中文本]()，光标置于括号内等待输入图片地址；
-- 无选区时插入 ![]()，光标置于方括号内等待输入 alt 文本。

if input == "" then
    insert("![]()", -3)
    return
end
insert("![" .. input .. "]()", -1)
