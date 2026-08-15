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
#endif

#include <cstring>
#include <string>
#include <vector>

namespace {

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
    static constexpr int kSidePad  = 18;   // min distance from left/right edges
    static constexpr int kMinW     = 420;  // min width (keeps QRs tidy)
    static constexpr int kMinH     = 460;
    // QR image size / gap (must match draw). SVG images are pre-scaled to an
    // Fl_RGB_Image at this resolution so they render fully (Fl_SVG_Image's own
    // on-the-fly downscale can draw only part of the art).
    static constexpr int kQrSize = 110;
    static constexpr int kQrGap  = 14;

    AboutPanel(int X, int Y, int W, int H, const Theme *theme, int fs,
               std::vector<std::string> lines)
        : Fl_Widget(X, Y, W, H), m_theme(theme), m_fs(fs), m_lines(std::move(lines)) {
        box(FL_NO_BOX);
        m_qr0Name = "微信";
        m_qr1Name = "支付宝";
        m_qr0 = qrReady("WeChatPay", m_qr0Name.c_str());
        m_qr1 = qrReady("ALiPay", m_qr1Name.c_str());
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
    static Fl_Image *qrReady(const char *base, const char *caption) {
        Fl_Image *img = loadImageBase(base);
        if (img) {
            if (dynamic_cast<Fl_SVG_Image *>(img)) {
                Fl_SVG_Image *svg = static_cast<Fl_SVG_Image *>(img);
                svg->resize(kQrSize, kQrSize);
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

    void draw() FL_OVERRIDE {
        fl_push_clip(x(), y(), w(), h());
        const int cx = x() + w() / 2;
        int y = this->y() + 12;

        fl_font(FL_HELVETICA, m_fs);
        for (size_t i = 0; i < m_lines.size(); ++i) {
            const std::string &s = m_lines[i];
            if (s.empty()) {
                y += m_fs / 2 + 4;   // a compact blank line
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
                y += 12;
                // Captions above each QR (centered over its own QR).
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
        : DialogBase(460, 600, I18n::get("menu.help.about"),
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
        std::string license = "Licensed under AGPL-3.0";
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
            "",         // 8. blank
            project,    // 9. gitee URL
            "",         // 10. blank
            donate,     // 11. sponsor
        };

        m_panel = new AboutPanel(0, TITLE_H, W, H - TITLE_H, theme, fs, std::move(lines));
        // QR captions above each code - always bilingual.
        m_panel->setQrCaptions("微信 / WeChat", "支付宝 / Alipay");

        end();
        finalizeShell();
        // Let the panel follow the window width for the show-time fit below.
        resizable(m_panel);
    }

    ~AboutDialog() override = default;

    void centerAndShow() {
        // Fit the window width to the widest line (measured once the graphics
        // context is ready), clamped to a minimum that keeps the QRs tidy and
        // a side margin from the window edges.
        int want = m_panel ? m_panel->contentWidth() : 0;
        int newW = want > 0 ? want : AboutPanel::kMinW;
        if (newW < AboutPanel::kMinW) newW = AboutPanel::kMinW;
        size(newW, h());
        position((Fl::w() - newW) / 2, (Fl::h() - h()) / 2);
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
