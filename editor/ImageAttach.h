// ImageAttach.h - 剪贴板里的图片/文件 → 落到文档旁边的附件目录 + Markdown 链接。
//
// 「设置-选项 → 粘贴图片/文件时插入路径」打开后，编辑器里的 Ctrl+V 会先问一句
// "剪贴板里有没有文本"：
//   · 有文本 → 一律交给原来的文本粘贴（从 Word/网页复制带图内容时，用户要
//     的几乎总是文字，抢过来插路径是最坏的行为）；
//   · 只有位图（截图工具 / 浏览器右键复制图片：CF_DIB，没有任何文件）
//     → 编码成 PNG 写进目录，插入 ![](目录/文件名)；
//   · 只有文件（资源管理器里 Ctrl+C：CF_HDROP）
//     → 复制一份进同一目录，图片用 ![](目录/名)、其它文件用 [名](目录/名)。
//
// 目录规则（2026-10-31 定）：素材目录**恒等于文档名去掉后缀**，没有可配置项——
//   D:\abc\bba.txt  →  D:\abc\bba\  →  链接 ![](bba/img_xxx.png)
// 同一目录下多个文档各有各的目录，拷走一篇文档连同它的目录就是一个完整
// 单元，不会和别人的图混在一起。曾经有过"自定义目录名"的设置项，但它唯一的
// 用途就是让多篇文档共用一个目录 —— 那正好是这个功能要解决的问题，故删除。
//
// 链接一律**相对文档目录**：应用内预览以 doc_dir 为基准解析
// （MyContainer::resolve_image_path），浏览器预览 / HTML 导出把相对图拷进
// build_dir 后按相对路径找。绝对路径在浏览器里必断（preprocess.cpp 第 7 步
// 显式跳过 `C:` 开头的拷贝），所以本项目**从不**生成绝对路径链接。
//
// 目录名要"消毒"（sanitizeDirName）：CommonMark 的裸链接目的地不允许出现
// 空格和括号，`![](我的 笔记(2026)/a.png)` 会直接解析失败。中文保留 ——
// md4c-html 输出时做 %XX 编码，容器侧 url_decode 能解回原文。
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace image_attach {

// ---------------------------------------------------------------------------
// 纯函数：门禁（test_image_attach）直接覆盖，不碰剪贴板也不碰磁盘
// ---------------------------------------------------------------------------

// 把一段名字里会破坏 Windows 文件名或 Markdown 链接的字符换成 '_'：
//   \ / : * ? " < > | 以及空格、括号、方括号、花括号、# % & ' ` ~ ! ^ ,
// 控制字符同样处理；ASCII 字母数字与 . _ - 保留，所有 >= 0x80 的字节
// （UTF-8 中文）原样保留。首尾的点与空格会被 Windows 悄悄吃掉，一并剥掉。
// 结果为空 → "attach"。
std::string sanitizeDirName(const std::string &name);

// 文档全路径（UTF-8）→ 素材目录名：取文件名、去最后一个 '.' 之后的后缀、
// 消毒。没有文件名或消毒后为空 → "attach"。
std::string defaultDirNameForDoc(const std::string &docPathUtf8);

// 扩展名白名单（带点，大小写不敏感）。预览能渲染的才算：png / jpg / jpeg /
// gif / bmp / svg。**webp 不在内** —— 解码链是 stb_image + resvg，没有
// webp 解码器，收进来只会得到一张显示不出来的图。
bool isImageExt(const std::string &ext);

// 位图 → 32bpp BGRA（WIC 的 GUID_WICPixelFormat32bppBGRA 内存序就是 BGRA）。
// 处理三件容易出错的事：
//   1. bottom-up：biHeight 为正时行序自下而上，必须翻转；
//   2. 24bpp：每行按 4 字节对齐，行尾有填充字节；
//   3. 32bpp 但 alpha 全 0 —— 很多截图工具给的 DIB 根本没有 alpha 语义，
//      照搬 4 字节当 RGBA 会得到一张全透明图。这里检测到"全 0"就按不透明处理。
// bits 必须长度足够（由调用方按 stride*height 预算），不足返回 false。
bool dibToBgra(const unsigned char *bits, size_t bitsSize,
               int width, int height, int bitCount, size_t stride, bool topDown,
               std::vector<unsigned char> &out);

// 时间戳文件名：img_20261031_184012.png（seq>0 时追加 -1 -2 … 用于去重）。
std::string stampName(int year, int month, int day, int hour, int minute,
                      int second, int seq, const char *ext);

// 插入到文档里的文本。relPath 用 '/' 分隔、相对文档目录。
std::string imageLink(const std::string &relPath);
std::string fileLink(const std::string &relPath, const std::string &display);

// ---------------------------------------------------------------------------
// 系统层：剪贴板 + PNG 落盘 + 宽窄字符转换（不做业务判断）
// ---------------------------------------------------------------------------

// 剪贴板里有纯文本吗？（有文本时调用方必须放行走普通粘贴）
bool clipboardHasText();

// 剪贴板位图 → BGRA。CF_DIBV5 优先，退回 CF_DIB。
bool clipboardReadImage(std::vector<unsigned char> &bgra, int &width, int &height);

// 剪贴板里的文件列表（CF_HDROP）。没有文件时返回空。
std::vector<std::wstring> clipboardReadFiles();

// BGRA → PNG 文件（WIC 编码，不引入第三方编码器）。
bool writePngBgra(const std::wstring &path, int width, int height,
                  const unsigned char *bgra);

std::wstring widen(const std::string &utf8);
std::string narrow(const std::wstring &wide);

} // namespace image_attach
