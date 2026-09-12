-- ==Meta==
-- @name Right: x chars
-- @name.zh-CN 右侧: x 字符
-- @name.zh-TW 右側: x 字符
-- @name.ja 右: x 文字
-- @name.de Rechts: x Zeichen
-- @name.fr Droite: x caractères
-- @name.es Derecha: x caracteres
-- @name.ru Справа: x символов
-- @name.ko 오른쪽: x 문자
-- @name.pt-BR Direita: x caracteres
-- @name.it Destra: x caratteri
-- @name.ar يمين: x أحرف
-- @name.hi दाईं ओर: x अक्षर
-- @name.id Kanan: x karakter
-- @name.tr Sağ: x karakter
-- @name.vi Phải: x ký tự
-- @author 邱宗满
-- @date 20260810
--!param count=10: number of characters to keep
--!pui.title = Take Right Chars
--!pui.title.zh-CN = 截取右侧字符
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
--!pui.count = Characters
--!pui.count.zh-CN = 字符数
--!pui.count.zh-TW = 字符數
--!pui.count.ja = 文字数
--!pui.count.de = Zeichenanzahl
--!pui.count.fr = Nombre de caractères
--!pui.count.es = Número de caracteres
--!pui.count.ru = Количество символов
--!pui.count.ko = 문자 수
--!pui.count.pt-BR = Número de caracteres
--!pui.count.it = Numero di caratteri
--!pui.count.ar = عدد الأحرف
--!pui.count.hi = अक्षर संख्या
--!pui.count.id = Jumlah Karakter
--!pui.count.tr = Karakter Sayısı
--!pui.count.vi = Số ký tự
-- ==/Meta==

-- Keep only the last N characters of every line (UTF-8 aware).

if #input == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the lines to process first.")
    end
    return
end

local count = tonumber(params.count) or 10
if count < 0 then count = 0 end

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
    local n = utf8.len(line)
    if count < n then
        local b = utf8.offset(line, n - count + 1)
        line = line:sub(b)
    end
    out[#out + 1] = line .. eol
    if not nl then break end
    start = nl + #eol
end
return table.concat(out, "")
