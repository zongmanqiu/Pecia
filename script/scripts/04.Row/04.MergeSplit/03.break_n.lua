-- ==Meta==
-- @name Split Every x Chars
-- @name.zh-CN 按 x 字符拆分
-- @name.zh-TW 按 x 字符拆分
-- @name.ja x 文字ごとに分割
-- @name.de Alle x Zeichen teilen
-- @name.fr Séparer tous les x caractères
-- @name.es Dividir cada x caracteres
-- @name.ru Разделить каждые x символов
-- @name.ko x 문자마다 분할
-- @name.pt-BR Dividir a cada x caracteres
-- @name.it Dividi ogni x caratteri
-- @name.ar تقسيم كل x أحرف
-- @name.hi हर x अक्षर पर विभाजित करें
-- @name.id Pisah Setiap x Karakter
-- @name.tr Her x Karakterde Ayır
-- @name.vi Tách mỗi x ký tự
-- @author 邱宗满
-- @date 20260810
--!param n=10: characters per line
--!pui.title = Break Lines by Chars
--!pui.title.zh-CN = 按字符数断行
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
--!pui.n = Characters per line
--!pui.n.zh-CN = 每行字符数
--!pui.n.zh-TW = 每組行數
--!pui.n.ja = グループ行数
--!pui.n.de = Zeilen pro Gruppe
--!pui.n.fr = Lignes par groupe
--!pui.n.es = Líneas por grupo
--!pui.n.ru = Строк на группу
--!pui.n.ko = 그룹별 행 수
--!pui.n.pt-BR = Linhas por grupo
--!pui.n.it = Righe per gruppo
--!pui.n.ar = أسطر لكل مجموعة
--!pui.n.hi = प्रति समूह पंक्तियाँ
--!pui.n.id = Baris per Grup
--!pui.n.tr = Grup Başına Satır
--!pui.n.vi = Dòng mỗi nhóm
-- ==/Meta==

-- Break every line after every N characters (UTF-8 aware - Chinese
-- characters are never split). The last partial segment stays as-is.

if #input == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the text to process first.")
    end
    return
end

local n = tonumber(params.n) or 10
if n < 1 then n = 1 end

-- Split one line into chunks of at most `n` characters.
local function chunksOf(s, n)
    local out = {}
    local i = 1
    local count = 0
    local start = 1
    while i <= #s do
        local b = s:byte(i)
        local clen = 1
        if b >= 0xF0 then clen = 4
        elseif b >= 0xE0 then clen = 3
        elseif b >= 0xC0 then clen = 2
        end
        count = count + 1
        if count == n then
            out[#out + 1] = s:sub(start, i + clen - 1)
            start = i + clen
            count = 0
        end
        i = i + clen
    end
    if count > 0 then
        out[#out + 1] = s:sub(start)
    end
    return out
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
    local chunks = chunksOf(line, n)
    out[#out + 1] = table.concat(chunks, "\n") .. eol
    if not nl then break end
    start = nl + #eol
end
return table.concat(out, "")
