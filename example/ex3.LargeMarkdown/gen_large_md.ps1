#requires -Version 5.1
<#
    gen_large_md.ps1 - generate a large Markdown document for Pecia preview stress tests.

    The whole body comes from one template; every @@N@@ placeholder is replaced with the
    section counter, so the file is structurally realistic (headings / lists / tables /
    fenced code / images) instead of one repeated paragraph.

    Why a level-1 heading every N sections: the side-panel outline and the preview block
    layout both walk the heading tree, so a document with a single h1 and 5000 h2 would not
    exercise the same code as a chaptered one.

    Output: UTF-8 without BOM, CRLF line endings (what a Windows editor writes).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$OutFile,
    [Parameter(Mandatory = $true)][long]$TargetBytes,
    [int]$SectionsPerChapter = 12
)

$ErrorActionPreference = 'Stop'
$nl = [string][char]13 + [string][char]10

# Here-strings keep whatever line endings this .ps1 has on disk, so normalise explicitly
# instead of trusting the file: every line must end in CRLF.
function Normalize([string]$s) {
    return $s.Replace("`r`n", "`n").Replace("`n", $nl)
}

$header = Normalize @'
# Pecia 大文档压力测试样例（自动生成，勿手工编辑）

> 生成脚本：`example/ex3.LargeMarkdown/gen_large_md.ps1`
> 用途：验证 Markdown 预览在分块渲染（HTML 超过 256KB 时启用）与超大文档下的打开耗时、
> 滚动流畅度与内存占用。
> 结构：每 @@SECTIONS@@ 节插入一个一级标题，构成章节层次（大纲与目录跳转需要层级）。

---
'@

$template = Normalize @'
## @@N@@. 第 @@N@@ 节 混排与排版

本节的用途是压测 Markdown 预览：中英文混排、行内标记、列表嵌套、表格、代码块与图片交替出现，
用于检验分块布局的切分点是否落在安全位置，以及文本宽度缓存能否稳定命中。

段落：`inline code`、**粗体**、*斜体*、~~删除线~~、[外链](https://example.com/pecia/@@N@@)、
以及一个必须被拆行的超长不可断词
https://example.com/path/@@N@@/a-very-long-token-without-any-spaces-that-must-be-wrapped 。

- 列表项 A（@@N@@）
- 列表项 B（@@N@@）
  - 嵌套项 B1（@@N@@）
  - 嵌套项 B2（@@N@@）
- 列表项 C（@@N@@）

> 引用块 @@N@@：预览是只读视图，滚动与目录跳转应保持可用。

| 序号 | 名称 | 数值 | 备注 |
|---:|---|---:|---|
| @@N@@ | 样本 @@N@@ | @@N@@00 | 表格行 |
| @@N@@ | 样本 @@N@@-1 | @@N@@01 | 表格行 |

```cpp
// 代码块 @@N@@
int scale(int n) { return n * 2; }
```

![样例图](../ex1.markdown/sample-png.png)
'@

# Exactly one blank line after each chunk, so headings never touch the previous block.
$header = $header.TrimEnd() + $nl + $nl
$template = $template.TrimEnd() + $nl + $nl

$dir = Split-Path -Parent $OutFile
if ($dir -and -not (Test-Path -LiteralPath $dir)) {
    New-Item -ItemType Directory -Path $dir | Out-Null
}

$enc = New-Object System.Text.UTF8Encoding($false)
$sw = New-Object System.IO.StreamWriter($OutFile, $false, $enc, 1048576)

$written = [long]0
$sections = 0
$chapters = 0

try {
    $h = $header.Replace('@@SECTIONS@@', [string]$SectionsPerChapter)
    $sw.Write($h)
    $written += $enc.GetByteCount($h)

    while ($written -lt $TargetBytes) {
        $sections++
        $block = ''
        if ((($sections - 1) % $SectionsPerChapter) -eq 0) {
            $chapters++
            $block += "# 第 $chapters 章 压力测试章节" + $nl + $nl
        }
        $block += $template.Replace('@@N@@', [string]$sections)
        $sw.Write($block)
        $written += $enc.GetByteCount($block)
    }
}
finally {
    $sw.Close()
}

$actual = (Get-Item -LiteralPath $OutFile).Length
Write-Output ("generated: {0}  {1} bytes  {2} sections  {3} chapters" -f `
        (Split-Path -Leaf $OutFile), $actual, $sections, $chapters)
