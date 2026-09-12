-- ==Meta==
-- @name After Char x
-- @name.zh-CN 第 x 字符后
-- @name.zh-TW 第 x 字符後
-- @name.ja x 文字目以降
-- @name.de Nach Zeichen x
-- @name.fr Après caractère x
-- @name.es Después del carácter x
-- @name.ru После символа x
-- @name.ko x번째 문자 뒤
-- @name.pt-BR Após caractere x
-- @name.it Dopo carattere x
-- @name.ar بعد الحرف x
-- @name.hi अक्षर x के बाद
-- @name.id Setelah Karakter x
-- @name.tr x Karakterinden Sonra
-- @name.vi Sau ký tự x
-- @author 邱宗满
-- @date 20260810
--!param pos=1: character index (1 = after the first character)
--!param text=: text to insert
--!pui.title = Insert at Position
--!pui.title.zh-CN = 第 x 字符后插入
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
--!pui.pos = Char index
--!pui.pos.zh-CN = 字符位置
--!pui.pos.zh-TW = 位置
--!pui.pos.ja = 位置
--!pui.pos.de = Position
--!pui.pos.fr = Position
--!pui.pos.es = Posición
--!pui.pos.ru = Позиция
--!pui.pos.ko = 위치
--!pui.pos.pt-BR = Posição
--!pui.pos.it = Posizione
--!pui.pos.ar = الموضع
--!pui.pos.hi = स्थिति
--!pui.pos.id = Posisi
--!pui.pos.tr = Konum
--!pui.pos.vi = Vị trí
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

-- Insert text after the Nth character (UTF-8 aware - counts characters,
-- not bytes; 0 = start of line, larger than the line = end of line).

if #input == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the lines to process first.")
    end
    return
end

local pos = tonumber(params.pos) or 1
if pos < 0 then pos = 0 end
local text = params.text or ""

local function charEndByte(line, count)
    -- Byte position right after the first `count` characters.
    local n = utf8.len(line)
    if count >= n then return #line + 1 end
    return utf8.offset(line, count + 1)
end

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
    local bpos = charEndByte(line, pos)
    out[#out + 1] = line:sub(1, bpos - 1) .. text .. line:sub(bpos) .. eol
    if not nl then break end
    start = nl + #eol
end
return table.concat(out, "")
