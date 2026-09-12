-- ==Meta==
-- @name Prefix
-- @name.zh-CN 前缀
-- @name.zh-TW 前綴
-- @name.ja プレフィックス
-- @name.de Präfix
-- @name.fr Préfixe
-- @name.es Prefijo
-- @name.ru Префикс
-- @name.ko 접두사
-- @name.pt-BR Prefixo
-- @name.it Prefisso
-- @name.ar بادئة
-- @name.hi उपसर्ग
-- @name.id Awalan
-- @name.tr Önek
-- @name.vi Tiền tố
-- @author 邱宗满
-- @date 20260810
--!param text=: text to insert
--!pui.title = Insert Prefix
--!pui.title.zh-CN = 插入前缀
--!pui.title.zh-TW = 標題
--!pui.title.ja = タイトル
--!pui.title.de = Titel
--!pui.title.fr = Titre
--!pui.title.es = Título
--!pui.title.ru = Заголовок
--!pui.title.ko = 제목
--!pui.title.pt-BR = Título
--!pui.title.it = Titolo
--!pui.title.ar = العنوان
--!pui.title.hi = शीर्षक
--!pui.title.id = Judul
--!pui.title.tr = Başlık
--!pui.title.vi = Tiêu đề
--!pui.text = Text
--!pui.text.zh-CN = 插入内容
--!pui.text.zh-TW = 文字
--!pui.text.ja = テキスト
--!pui.text.de = Text
--!pui.text.fr = Texte
--!pui.text.es = Texto
--!pui.text.ru = Текст
--!pui.text.ko = 텍스트
--!pui.text.pt-BR = Texto
--!pui.text.it = Testo
--!pui.text.ar = نص
--!pui.text.hi = पाठ
--!pui.text.id = Teks
--!pui.text.tr = Metin
--!pui.text.vi = Văn bản
-- ==/Meta==

-- Prepend the given text to every line of the selection.

if #input == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the lines to process first.")
    end
    return
end

local text = params.text or ""

local out = {}
local start = 1
while start <= #input do
    local nl = input:find("[\r\n]", start)
    local line, eol = "", ""
    if nl then
        line = input:sub(start, nl - 1)
        eol = input:sub(nl, nl)
        if eol == "\r" and input:sub(nl + 1, nl + 1) == "\n" then eol = "\r\n" end
    else
        line = input:sub(start)
    end
    out[#out + 1] = text .. line .. eol
    if not nl then break end
    start = nl + #eol
end
return table.concat(out, "")
