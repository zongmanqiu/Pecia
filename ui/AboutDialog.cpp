// AboutDialog.cpp - "About Pecia" dialog.
// Derived from DialogBase (same TitleBar/caption-button shell, border and
// theme as every other Pecia window). The whole content area is ONE
// self-drawn widget (AboutPanel) that paints every text element with fl_draw
// at explicit local coordinates - this avoids the multi-child / Fl_Box label
// alignment issues that previously made the About text spill or disappear.
#include "ui/AboutDialog.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "ui/DialogBase.h"
#include "ui/SmokeTest.h"

#include <FL/Fl_Widget.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_SVG_Image.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#if defined(_WIN32)
#include <shellapi.h>
#include <windows.h>
#include "resource.h"   // ID_QR_WECHAT / ID_QR_ALIPAY (embedded QR SVGs)
#endif

#include <cstring>
#include <string>
#include <vector>

namespace {

// Load an SVG QR code embedded as a Windows RCDATA resource (see pecia.rc and
// resource.h). Returns an Fl_SVG_Image built from the in-memory bytes, or
// nullptr if the resource is absent. Fl_SVG_Image copies the data internally,
// so the transient buffer does not need to outlive this call.
Fl_Image *loadImageResource(int resid) {
#if defined(_WIN32)
    HRSRC hrsc = FindResourceW(nullptr, MAKEINTRESOURCEW(resid), MAKEINTRESOURCEW(10)); // RT_RCDATA
    if (!hrsc) return nullptr;
    HGLOBAL hg = LoadResource(nullptr, hrsc);
    if (!hg) return nullptr;
    void *data = LockResource(hg);
    DWORD len = SizeofResource(nullptr, hrsc);
    if (!data || len == 0) return nullptr;
    Fl_SVG_Image *img = new Fl_SVG_Image(nullptr, static_cast<const unsigned char *>(data),
                                         static_cast<size_t>(len));
    // Construction fails silently into an empty image on a parse error.
    if (img->w() > 0) return img;
    delete img;
#endif
    return nullptr;
}

// Load exeDir/image/<base> as a Fl_Image*, trying .svg then .png. The caller
// passes the URL-encoded base name (no extension). Returns nullptr if absent.
Fl_Image *loadImageBase(const char *base) {
    wchar_t exe[MAX_PATH];
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return nullptr;
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (!slash) return nullptr;
    *(slash + 1) = 0;
    char narrow[MAX_PATH * 2];
    auto narrowize = [&](const wchar_t *p) -> const char * {
        int n = WideCharToMultiByte(CP_UTF8, 0, p, -1, narrow,
                                    (int)sizeof(narrow), nullptr, nullptr);
        return n > 0 ? narrow : nullptr;
    };
    swprintf(path, MAX_PATH, L"%simage\\%hs.svg", exe, base);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
        const char *np = narrowize(path);
        if (np) {
            Fl_SVG_Image *img = new Fl_SVG_Image(np);
            if (img->w() > 0) return img;
            delete img;
        }
    }
    swprintf(path, MAX_PATH, L"%simage\\%hs.png", exe, base);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
        const char *np = narrowize(path);
        if (np) {
            Fl_PNG_Image *img = new Fl_PNG_Image(np);
            if (img->w() > 0) return img;
            delete img;
        }
    }
    return nullptr;
}

// Placeholder for a missing QR image.
Fl_Image *placeholderImage(const char *caption) {
    const int SZ = 200;
    Fl_Image_Surface surf(SZ, SZ);
    Fl_Surface_Device::push_current(&surf);
    fl_color(0xEC, 0xEC, 0xEC);
    fl_rectf(0, 0, SZ, SZ);
    fl_color(0xC0, 0xC0, 0xC0);
    fl_rect(0, 0, SZ, SZ);
    fl_color(0x9A, 0x9A, 0x9A);
    fl_font(FL_HELVETICA, 12);
    fl_draw(caption, 0, 0, SZ, SZ, FL_ALIGN_CENTER);
    Fl_Surface_Device::pop_current();
    return surf.image();
}

// The entire About content, self-drawn in one widget so nothing can drift.
// A single centered column of lines (empty entries = blank lines). The first
// non-empty line is the app title (large), the rest are body lines. QRs sit
// centered at the bottom. Width adapts to the widest line (with side padding),
// clamped to a minimum width that fits the QRs.
class AboutPanel : public Fl_Widget {
public:
    // Width / layout constants (referenced by AboutDialog for window sizing).
    static constexpr int kSidePad  = 10;   // min distance from left/right edges
    // QR image size / gap (must match draw). SVG images are pre-scaled to an
    // Fl_RGB_Image at this resolution so they render fully (Fl_SVG_Image's own
    // on-the-fly downscale can draw only part of the art).
    static constexpr int kQrSize = 160;
    // Gap between the two QR codes. Kept generous so one code is not
    // accidentally scanned while aiming at the other.
    static constexpr int kQrGap  = 28;
    // Minimum window width: must fit the two QRs (2 * kQrSize) plus the QR gap
    // plus kSidePad on each side.
    static constexpr int kMinW     = 2 * kQrSize + kQrGap + 2 * kSidePad;  // = 368
    static constexpr int kMinH     = 490;
    // Desired breathing room between the QR block and the window's bottom
    // border line (used when computing the adaptive window height).
    static constexpr int kBottomPad = 5;

    AboutPanel(int X, int Y, int W, int H, const Theme *theme, int fs,
               std::vector<std::string> lines)
        : Fl_Widget(X, Y, W, H), m_theme(theme), m_fs(fs), m_lines(std::move(lines)) {
        box(FL_NO_BOX);
        m_qr0Name = "微信";
        m_qr1Name = "支付宝";
        m_qr0 = qrReady(ID_QR_WECHAT, "WeChatPay", m_qr0Name.c_str());
        m_qr1 = qrReady(ID_QR_ALIPAY, "ALiPay", m_qr1Name.c_str());
    }

    // Localized label drawn above each QR (wechatName / alipayName).
    void setQrCaptions(const char *wechat, const char *alipay) {
        if (wechat && *wechat) m_qr0Name = wechat;
        if (alipay && *alipay) m_qr1Name = alipay;
    }

    ~AboutPanel() override {
        delete m_qr0;
        delete m_qr1;
    }

    // Build a raster that draws correctly at kQrSize: SVG -> pre-rasterized at
    // kQrSize (resize() forces the nanosvg rasterization to that resolution,
    // so drawing it is 1:1 and complete); PNG -> as-is; missing -> placeholder.
    // Prefers the QR SVG embedded in the exe (resid) and falls back to an
    // exe-adjacent image/<base> file for compatibility.
    static Fl_Image *qrReady(int resid, const char *base, const char *caption) {
        Fl_Image *img = loadImageResource(resid);
        if (img) {
            if (dynamic_cast<Fl_SVG_Image *>(img)) {
                static_cast<Fl_SVG_Image *>(img)->resize(kQrSize, kQrSize);
            }
            return img;
        }
        img = loadImageBase(base);
        if (img) {
            if (dynamic_cast<Fl_SVG_Image *>(img)) {
                static_cast<Fl_SVG_Image *>(img)->resize(kQrSize, kQrSize);
            }
            return img;
        }
        return placeholderImage(caption);
    }

    // Widest line (pixels) at the current font. Called at show time when the
    // graphics context is ready so fl_measure is reliable.
    int contentWidth() const {
        fl_font(FL_HELVETICA, m_fs);
        int maxW = 0;
        for (const auto &s : m_lines) {
            int tw = 0, th = 0;
            fl_measure(s.c_str(), tw, th);
            if (tw > maxW) maxW = tw;
        }
        return maxW + 2 * kSidePad;
    }

    // Height of the content (relative to the panel top = top padding + text
    // lines + QR block), mirroring the exact layout advance logic in draw().
    // Used by centerAndShow() to size the window so a fixed bottom margin is
    // kept whatever the font size / title bar height.
    int contentHeight() const {
        int y = 12;   // top padding (matches draw()'s starting offset)
        for (size_t i = 0; i < m_lines.size(); ++i) {
            const std::string &s = m_lines[i];
            if (s.empty()) {
                y += 2 * (m_fs / 2 + 4);
                continue;
            }
            bool isTitle = (i == 0);
            y += (isTitle ? m_fs + 14 : m_fs) + 8;
            (void)s;
        }
        if (m_qr0 && m_qr1) {
            // qy = (y at end of last line) + m_fs + 8, then the QR box height.
            y += m_fs + 8 + kQrSize;
        }
        return y;
    }

    void draw() FL_OVERRIDE {
        fl_push_clip(x(), y(), w(), h());
        const int cx = x() + w() / 2;
        int y = this->y() + 12;

        fl_font(FL_HELVETICA, m_fs);
        for (size_t i = 0; i < m_lines.size(); ++i) {
            const std::string &s = m_lines[i];
            if (s.empty()) {
                y += 2 * (m_fs / 2 + 4);   // a blank gap worth two empty lines
                continue;
            }
            // First line = app title (large, bold); rest body text.
            bool isTitle = (i == 0);
            fl_font(isTitle ? FL_HELVETICA_BOLD : FL_HELVETICA,
                    isTitle ? m_fs + 14 : m_fs);
            fl_color(m_theme ? m_theme->colors().textPrimary : FL_BLACK);
            fl_draw(s.c_str(), cx - (int)fl_width(s.c_str()) / 2, y + (isTitle ? m_fs + 14 : m_fs));
            y += (isTitle ? m_fs + 14 : m_fs) + 8;   // line height + small gap
        }

        // Two QR images side by side, centered at the bottom, each with a
        // caption above it (clipped to the panel).
        if (m_qr0 && m_qr1) {
            const int totalW = 2 * kQrSize + kQrGap;
            int x0 = x() + (w() - totalW) / 2;
            if (x0 >= x() && x0 + totalW <= x() + w()) {
                // No blank gap between the sponsor line and the QR captions;
                // the captions sit directly on the following line.
                fl_font(FL_HELVETICA, m_fs);
                fl_color(m_theme ? m_theme->colors().textPrimary : FL_BLACK);
                int cap0 = (int)fl_width(m_qr0Name.c_str());
                fl_draw(m_qr0Name.c_str(), x0 + kQrSize / 2 - cap0 / 2, y + m_fs);
                int cap1 = (int)fl_width(m_qr1Name.c_str());
                fl_draw(m_qr1Name.c_str(), x0 + kQrSize + kQrGap + kQrSize / 2 - cap1 / 2, y + m_fs);
                int qy = y + m_fs + 8;   // images below the captions
                fl_color(FL_BLACK);
                fl_draw_box(FL_FLAT_BOX, x0, qy, kQrSize, kQrSize, FL_WHITE);
                m_qr0->draw(x0, qy, kQrSize, kQrSize, 0, 0);
                fl_draw_box(FL_FLAT_BOX, x0 + kQrSize + kQrGap, qy, kQrSize, kQrSize, FL_WHITE);
                m_qr1->draw(x0 + kQrSize + kQrGap, qy, kQrSize, kQrSize, 0, 0);
            }
        }
        fl_pop_clip();
    }

private:
    const Theme *m_theme;
    int m_fs;
    std::vector<std::string> m_lines;
    Fl_Image *m_qr0 = nullptr;
    Fl_Image *m_qr1 = nullptr;
    std::string m_qr0Name, m_qr1Name;
};

// The dialog itself.
class AboutDialog : public DialogBase {
public:
    AboutDialog(const Theme *theme, int uiFontSize)
        : DialogBase(AboutPanel::kMinW, 520, I18n::get("menu.help.about"),
                     theme, uiFontSize, ModalDialog) {
        begin();
        initShell(I18n::get("menu.help.about"));

        const int fs = uiFontSize ? uiFontSize : 14;
        const int W = AboutPanel::kMinW;
        const int H = AboutPanel::kMinH;

        const char *appName = I18n::get("app.name");
        std::string title = (appName && *appName && strcmp(appName, "app.name") != 0)
                                ? std::string(appName) : "Pecia";
        title += " 1.0.0";
        const char *tagRaw = I18n::get("about.tagline");
        std::string slogan = (tagRaw && *tagRaw && strcmp(tagRaw, "about.tagline") != 0)
                                 ? std::string(tagRaw) : "";
        bool zh = I18n::currentCode() && strcmp(I18n::currentCode(), "zh-CN") == 0;
        std::string copyright = zh ? "Copyright © 2025 邱宗满"
                                   : "Copyright © 2025 Qiu Zongman";
        std::string license = I18n::getOr("about.licenseLine", "Licensed under AGPL-3.0");
        std::string stack = "Built with C++ & FLTK  \xC2\xB7  AI: DeepSeek";
        if (zh) stack = "开发工具：C++ & FLTK  \xC2\xB7  AI: DeepSeek";
        std::string project = "https://gitee.com/qiuzongman/pecia";
        const char *donateRaw = I18n::get("about.donate");
        std::string donate = (donateRaw && *donateRaw && strcmp(donateRaw, "about.donate") != 0)
                                 ? std::string(donateRaw) : "";

        std::vector<std::string> lines = {
            title,      // 1. name + version
            slogan,     // 2. slogan
            "",         // 3. blank
            copyright,  // 4. copyright w/ author
            license,    // 5. license
            "",         // 6. blank
            stack,      // 7. dev tools + AI
            project,    // 8. gitee URL  (directly under dev tools)
            "",         // 9. blank
            donate,     // 10. sponsor
        };

        m_panel = new AboutPanel(0, TITLE_H, W, H - TITLE_H, theme, fs, std::move(lines));
        // QR captions above each code - one localized word per QR, in the
        // active interface language only (no bilingual "WeChat / 微信" pairing).
        m_panel->setQrCaptions(I18n::getOr("about.wechat", "微信"),
                               I18n::getOr("about.alipay", "支付宝"));

        end();
        finalizeShell();
        // The panel is explicitly resized to the full window width inside
        // centerAndShow(); do NOT attach it as the window resizable, otherwise
        // shrinking the window in centerAndShow() would also shrink the panel
        // and leave it narrower than the window (content off-centre).
    }

    ~AboutDialog() override = default;

    void centerAndShow() {
        // Fit the window width to the widest line (measured once the graphics
        // context is ready), clamped to a minimum that keeps the QRs tidy and
        // a side margin from the window edges.
        int want = m_panel ? m_panel->contentWidth() : 0;
        int newW = want > 0 ? want : AboutPanel::kMinW;
        if (newW < AboutPanel::kMinW) newW = AboutPanel::kMinW;
        // Reference the modal 1px border: the content sits 1px inside the
        // window, so the panel spans the full width to keep every line and the
        // QR block centered on the true window centre.
        const int bd = 1;
        // Size the height from the actual content so the bottom margin stays a
        // small fixed value regardless of the ui font size / title bar height.
        const int bottomPad = AboutPanel::kBottomPad;
        const int ch = m_panel ? m_panel->contentHeight() : AboutPanel::kMinH;
        const int newH = ch + bottomPad + TITLE_H + 2 * bd;
        m_panel->resize(bd, TITLE_H, newW - 2 * bd, newH - TITLE_H - bd);
        size(newW, newH);
        position((Fl::w() - newW) / 2, (Fl::h() - newH) / 2);
        show();
    }

private:
    AboutPanel *m_panel = nullptr;
};

} // namespace

void showAboutDialog(const Theme *theme, int uiFontSize) {
    if (!theme) return;
    AboutDialog dlg(theme, uiFontSize);
    dlg.centerAndShow();
#if defined(_WIN32)
    HWND hwnd = fl_xid(&dlg);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_APPWINDOW);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
#endif
    if (ui::g_smokeMode) { dlg.hide(); return; }   // smoke test: build+show only
    while (dlg.shown()) Fl::wait();
}
