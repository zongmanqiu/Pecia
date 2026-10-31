// ImageAttach.cpp - 剪贴板位图/文件 → PNG 落盘 + 纯逻辑辅助（见 ImageAttach.h）。
//
// 编码器选 WIC 而不是 stb_image_write：stb 的实现体只在 mdview/preprocess.cpp
// 里编了一份（STB_IMAGE_WRITE_IMPLEMENTATION），而本文件要被 test_match_highlight
// 这类只链 Editor 的目标一起编进去 —— 再引一个 stb 实现体就是重复符号，反过来
// 把 preprocess.cpp 拖进测试目标又会连带 litehtml/md4c。WIC 是系统组件，
// windowscodecs.lib 每个 Windows SDK 都有，用 pragma 就地声明，CMake 不用改。
#include "ImageAttach.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>     // DragQueryFileW
#include <wincodec.h>     // WIC PNG 编码

#pragma comment(lib, "windowscodecs.lib")

#include <cstdio>
#include <cstring>

namespace image_attach {

namespace {

// 需要换成 '_' 的字符：Windows 文件名非法字符 + 会破坏 CommonMark 裸链接
// 目的地的字符（空格、括号等），外加几个在 URL 里有特殊含义的（# % &）。
bool needUnderscore(unsigned char c) {
    if (c < 0x20 || c == 0x7F) return true;
    if (c >= 0x80) return false;              // UTF-8 中文：原样保留
    switch (c) {
        case '\\': case '/': case ':': case '*': case '?': case '"':
        case '<': case '>': case '|':
        case ' ': case '\t':
        case '(': case ')': case '[': case ']': case '{': case '}':
        case '#': case '%': case '&': case '\'': case '`':
            return true;
        default:
            return false;
    }
}

bool isReservedDeviceName(const std::string &lower) {
    static const char *kNames[] = {
        "con", "prn", "aux", "nul",
        "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
        "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
    };
    for (const char *n : kNames)
        if (lower == n) return true;
    return false;
}

bool decodeDibHandle(HANDLE h, std::vector<unsigned char> &bgra, int &width,
                     int &height) {
    if (!h) return false;
    const unsigned char *p = static_cast<const unsigned char *>(GlobalLock(h));
    if (!p) return false;
    const SIZE_T total = GlobalSize(h);
    bool ok = false;
    if (total >= sizeof(BITMAPINFOHEADER)) {
        const BITMAPINFOHEADER *bi =
            reinterpret_cast<const BITMAPINFOHEADER *>(p);
        const bool bitsOk = bi->biBitCount == 24 || bi->biBitCount == 32;
        const bool compOk = bi->biCompression == BI_RGB ||
                            bi->biCompression == BI_BITFIELDS;
        if (bi->biSize >= sizeof(BITMAPINFOHEADER) && bi->biSize <= total &&
            bi->biWidth > 0 && bi->biHeight != 0 && bi->biPlanes == 1 &&
            bitsOk && compOk) {
            size_t off = bi->biSize;
            // BI_BITFIELDS + 40 字节头：3 个 DWORD 掩码紧跟其后（112 字节的
            // V4/V5 头已经把掩码算在 biSize 里了，不能重复加）。
            if (bi->biCompression == BI_BITFIELDS &&
                bi->biSize == sizeof(BITMAPINFOHEADER))
                off += 12;
            const int pxH = bi->biHeight < 0 ? -bi->biHeight : bi->biHeight;
            if (off < total) {
                ok = dibToBgra(p + off, static_cast<size_t>(total - off),
                               bi->biWidth, pxH, bi->biBitCount, 0,
                               bi->biHeight < 0, bgra);
                if (ok) { width = bi->biWidth; height = pxH; }
            }
        }
    }
    GlobalUnlock(h);
    return ok;
}

} // namespace

std::string sanitizeDirName(const std::string &name) {
    std::string out;
    out.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (needUnderscore(c)) {
            // 连续的可疑字符折叠成一个 '_'，避免 "a   b" → "a___b"
            if (out.empty() || out.back() != '_') out += '_';
        } else {
            out += static_cast<char>(c);
        }
    }
    // Windows 会悄悄吃掉结尾的点和空格；开头是点则是隐藏目录，一并剥掉。
    // 首尾的 '_' 同样剥掉：它是"这里原本有空格/括号"的痕迹，留在名字两端只会
    // 让 `my_note_` 这种目录名看着像截断。剥完可能变空，交给下面的兜底。
    while (!out.empty() && (out.back() == '.' || out.back() == ' ' ||
                            out.back() == '_'))
        out.pop_back();
    while (!out.empty() && (out.front() == '.' || out.front() == ' ' ||
                            out.front() == '_'))
        out.erase(0, 1);
    if (out.empty()) return "attach";
    std::string lower = out;
    for (char &c : lower)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (isReservedDeviceName(lower)) out += '_';
    return out;
}

std::string defaultDirNameForDoc(const std::string &docPathUtf8) {
    std::string base = docPathUtf8;
    const size_t sep = base.find_last_of("\\/");
    if (sep != std::string::npos) base = base.substr(sep + 1);
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos && dot > 0) base = base.substr(0, dot);
    return sanitizeDirName(base);
}

bool isImageExt(const std::string &ext) {
    std::string e = ext;
    for (char &c : e)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".gif" ||
           e == ".bmp" || e == ".svg";
}

bool dibToBgra(const unsigned char *bits, size_t bitsSize, int width, int height,
               int bitCount, size_t stride, bool topDown,
               std::vector<unsigned char> &out) {
    if (!bits || width <= 0 || height <= 0) return false;
    if (bitCount != 24 && bitCount != 32) return false;
    // DIB 行按 4 字节对齐：24bpp 每行尾部有填充字节
    const size_t rowBytes =
        ((static_cast<size_t>(width) * static_cast<size_t>(bitCount) + 31) / 32) * 4;
    if (stride == 0) stride = rowBytes;
    if (stride < rowBytes) return false;
    const size_t need = stride * static_cast<size_t>(height - 1) + rowBytes;
    if (bitsSize < need) return false;

    out.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0xFF);
    bool anyAlpha = false;
    const int bytesPerPixel = bitCount / 8;
    for (int y = 0; y < height; ++y) {
        const int srcY = topDown ? y : (height - 1 - y);
        const unsigned char *src = bits + static_cast<size_t>(srcY) * stride;
        unsigned char *dst = out.data() + static_cast<size_t>(y) *
                                                static_cast<size_t>(width) * 4;
        for (int x = 0; x < width; ++x) {
            dst[0] = src[0];   // B
            dst[1] = src[1];   // G
            dst[2] = src[2];   // R
            if (bitCount == 32) {
                dst[3] = src[3];
                if (src[3] != 0) anyAlpha = true;
            }
            src += bytesPerPixel;
            dst += 4;
        }
    }
    // 32bpp 但整张图 alpha 全 0：这个 DIB 没有 alpha 语义（截图工具常见），
    // 照搬会存成一张全透明的 PNG。按不透明处理。
    if (bitCount == 32 && !anyAlpha)
        for (size_t i = 3; i < out.size(); i += 4) out[i] = 0xFF;
    return true;
}

std::string stampName(int year, int month, int day, int hour, int minute,
                      int second, int seq, const char *ext) {
    char buf[96];
    const char *e = (ext && *ext) ? ext : ".png";
    if (seq > 0)
        snprintf(buf, sizeof(buf), "img_%04d%02d%02d_%02d%02d%02d-%d%s", year,
                 month, day, hour, minute, second, seq, e);
    else
        snprintf(buf, sizeof(buf), "img_%04d%02d%02d_%02d%02d%02d%s", year,
                 month, day, hour, minute, second, e);
    return std::string(buf);
}

std::string imageLink(const std::string &relPath) {
    return std::string("![](") + relPath + ")";
}

std::string fileLink(const std::string &relPath, const std::string &display) {
    return std::string("[") + (display.empty() ? relPath : display) + "](" +
           relPath + ")";
}

bool clipboardHasText() {
    // 打不开剪贴板（别的进程正占着）时保守地当作"有文本"：宁可走普通粘贴，
    // 也不要把用户原本要粘的文字吞掉。
    if (!OpenClipboard(nullptr)) return true;
    const bool has = IsClipboardFormatAvailable(CF_UNICODETEXT) != 0;
    CloseClipboard();
    return has;
}

bool clipboardReadImage(std::vector<unsigned char> &bgra, int &width, int &height) {
    width = height = 0;
    if (!OpenClipboard(nullptr)) return false;
    bool ok = decodeDibHandle(GetClipboardData(CF_DIBV5), bgra, width, height);
    if (!ok) ok = decodeDibHandle(GetClipboardData(CF_DIB), bgra, width, height);
    CloseClipboard();
    return ok;
}

std::vector<std::wstring> clipboardReadFiles() {
    std::vector<std::wstring> out;
    if (!OpenClipboard(nullptr)) return out;
    HANDLE h = GetClipboardData(CF_HDROP);
    if (h) {
        const UINT n = DragQueryFileW(static_cast<HDROP>(h), 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; ++i) {
            const UINT len = DragQueryFileW(static_cast<HDROP>(h), i, nullptr, 0);
            if (len == 0) continue;
            std::wstring s(static_cast<size_t>(len) + 1, L'\0');
            const UINT got = DragQueryFileW(static_cast<HDROP>(h), i, &s[0], len + 1);
            if (got == 0) continue;
            s.resize(got);
            out.push_back(s);
        }
    }
    CloseClipboard();
    return out;
}

bool writePngBgra(const std::wstring &path, int width, int height,
                  const unsigned char *bgra) {
    if (path.empty() || width <= 0 || height <= 0 || !bgra) return false;

    // 主程序启动时已经 CoInitializeEx(STA)；测试/工具进程可能没有。
    // RPC_E_CHANGED_MODE 表示已有别的模型，照样能用，只是不归我们 Uninit。
    const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;

    IWICImagingFactory *factory = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&factory))) && factory) {
        const UINT w = static_cast<UINT>(width);
        const UINT h = static_cast<UINT>(height);
        const UINT stride = w * 4;
        IWICBitmap *bitmap = nullptr;
        if (SUCCEEDED(factory->CreateBitmapFromMemory(
                w, h, GUID_WICPixelFormat32bppBGRA, stride, stride * h,
                const_cast<BYTE *>(bgra), &bitmap)) && bitmap) {
            IWICStream *stream = nullptr;
            if (SUCCEEDED(factory->CreateStream(&stream)) && stream) {
                if (SUCCEEDED(stream->InitializeFromFilename(path.c_str(),
                                                            GENERIC_WRITE))) {
                    IWICBitmapEncoder *enc = nullptr;
                    if (SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,
                                                        nullptr, &enc)) && enc) {
                        if (SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache))) {
                            IWICBitmapFrameEncode *frame = nullptr;
                            IPropertyBag2 *props = nullptr;
                            if (SUCCEEDED(enc->CreateNewFrame(&frame, &props)) && frame) {
                                if (SUCCEEDED(frame->Initialize(props)) &&
                                    SUCCEEDED(frame->WriteSource(bitmap, nullptr)) &&
                                    SUCCEEDED(frame->Commit()) &&
                                    SUCCEEDED(enc->Commit()))
                                    ok = true;
                                frame->Release();
                            }
                            if (props) props->Release();
                        }
                        enc->Release();
                    }
                }
                stream->Release();
            }
            bitmap->Release();
        }
        factory->Release();
    }
    if (SUCCEEDED(hrInit)) CoUninitialize();
    return ok;
}

std::wstring widen(const std::string &utf8) {
    if (utf8.empty()) return std::wstring();
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                        static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                        &out[0], len);
    return out;
}

std::string narrow(const std::wstring &wide) {
    if (wide.empty()) return std::string();
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                        static_cast<int>(wide.size()), nullptr, 0,
                                        nullptr, nullptr);
    if (len <= 0) return std::string();
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                        &out[0], len, nullptr, nullptr);
    return out;
}

} // namespace image_attach
