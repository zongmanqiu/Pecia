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

// Load exeDir/image/<name> as a Fl_Image*. Returns nullptr if unavailable.
Fl_Image *loadImage(const char *name) {
    wchar_t exe[MAX_PATH];
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return nullptr;
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (!slash) return nullptr;
    *(slash + 1) = 0;
    swprintf(path, MAX_PATH, L"%simage\\%hs", exe, name);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return nullptr;
    char narrow[MAX_PATH * 2];
    int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow,
                                (int)sizeof(narrow), nullptr, nullptr);
    if (n <= 0) return nullptr;
    Fl_PNG_Image *img = new Fl_PNG_Image(narrow);
    if (img->w() <= 0) { delete img; return nullptr; }
    return img;
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
class AboutPanel : public Fl_Widget {
public:
    AboutPanel(int X, int Y, int W, int H, const Theme *theme, int fs,
               const char *title, const char *tag,
               std::vector<std::pair<const char *, const char *>> rows,
               const char *donate)
        : Fl_Widget(X, Y, W, H), m_theme(theme), m_fs(fs)
        , m_title(title ? title : ""), m_tag(tag ? tag : "")
        , m_donate(donate ? donate : "") {
        for (auto &r : rows)
            m_rows.push_back({ std::string(r.first), std::string(r.second) });
        box(FL_NO_BOX);
        m_qr0 = loadImage("donate_wechat.png");
        if (!m_qr0) m_qr0 = placeholderImage("WeChat");
        m_qr1 = loadImage("donate_alipay.png");
        if (!m_qr1) m_qr1 = placeholderImage("Alipay");
    }

    ~AboutPanel() override {
        delete m_qr0;
        delete m_qr1;
    }

    void draw() FL_OVERRIDE {
        // All coordinates are relative to this panel (x(), y(), w()), so the
        // layout never depends on absolute window coordinates.
        const int cw = w();
        const int pad = 20;
        const int rowh = 26;

        // 1. Big centered title.
        fl_font(FL_HELVETICA_BOLD, m_fs + 14);
        fl_color(m_theme ? m_theme->colors().textPrimary : FL_BLACK);
        int ty = y() + 26;
        if (!m_title.empty())
            fl_draw(m_title.c_str(), x(), ty, cw, m_fs + 14, FL_ALIGN_CENTER);

        // 2. Tagline.
        fl_font(FL_HELVETICA, m_fs);
        fl_color(m_theme ? m_theme->colors().textSecondary : fl_rgb_color(140, 140, 140));
        if (!m_tag.empty())
            fl_draw(m_tag.c_str(), x(), ty + (m_fs + 18), cw, m_fs, FL_ALIGN_CENTER);

        // 3. Info rows: label (secondary, left) + value (primary, right).
        fl_font(FL_HELVETICA, m_fs);
        int infoY = y() + (m_fs + 18) + (m_fs + 14) + 30;
        int colL = x() + pad;
        int colR = x() + cw - pad;
        for (size_t i = 0; i < m_rows.size(); ++i) {
            int ly = infoY + (int)i * rowh + m_fs + 4;
            fl_color(m_theme ? m_theme->colors().textSecondary : fl_rgb_color(140, 140, 140));
            fl_draw(m_rows[i].first.c_str(), colL, ly);
            int vw = (int)fl_width(m_rows[i].second.c_str());
            fl_color(m_theme ? m_theme->colors().textPrimary : FL_BLACK);
            fl_draw(m_rows[i].second.c_str(), colR - vw, ly);
        }

        // 4. Donate (centered).
        int donateY = infoY + (int)m_rows.size() * rowh + 30;
        fl_font(FL_HELVETICA, m_fs);
        fl_color(m_theme ? m_theme->colors().textPrimary : FL_BLACK);
        if (!m_donate.empty())
            fl_draw(m_donate.c_str(), x(), donateY, cw, m_fs, FL_ALIGN_CENTER);

        // 5. Two QR images side by side, centered.
        if (m_qr0 && m_qr1) {
            const int QR_SZ = 150;
            const int gap = 16;
            int totalW = 2 * QR_SZ + gap;
            int x0 = x() + (cw - totalW) / 2;
            int qry = donateY + m_fs + 24;
            fl_color(FL_BLACK);
            fl_draw_box(FL_FLAT_BOX, x0, qry, QR_SZ, QR_SZ, FL_WHITE);
            m_qr0->draw(x0, qry, QR_SZ, QR_SZ, 0, 0);
            m_qr1->draw(x0 + QR_SZ + gap, qry, QR_SZ, QR_SZ, 0, 0);
        }
    }

private:
    const Theme *m_theme;
    int m_fs;
    std::string m_title, m_tag, m_donate;
    std::vector<std::pair<std::string, std::string>> m_rows;
    Fl_Image *m_qr0 = nullptr;
    Fl_Image *m_qr1 = nullptr;
};

// The dialog itself.
class AboutDialog : public DialogBase {
public:
    AboutDialog(const Theme *theme, int uiFontSize)
        : DialogBase(500, 600, I18n::get("menu.help.about"),
                     theme, uiFontSize, ModalDialog) {
        begin();
        initShell(I18n::get("menu.help.about"));

        const int fs = uiFontSize ? uiFontSize : 14;
        const int W = 500;
        const int H = 600;

        const char *appName = I18n::get("app.name");
        std::string title = (appName && *appName && strcmp(appName, "app.name") != 0)
                                ? std::string(appName) : "Pecia 1.0.0";
        const char *tagRaw = I18n::get("about.tagline");
        std::string tag = (tagRaw && *tagRaw && strcmp(tagRaw, "about.tagline") != 0)
                              ? std::string(tagRaw) : "";
        std::vector<std::pair<const char*, const char*>> rows = {
            { I18n::get("about.author"),  "邱宗满 (Qiu Zongman)" },
            { I18n::get("about.email"),   "qiuzongman@foxmail.com" },
            { I18n::get("about.project"), "https://gitee.com/qiuzongman/pecia" },
            { I18n::get("about.license"), "AGPL-3.0" },
            { I18n::get("about.ai"),      "DeepSeek" },
        };
        const char *donate = I18n::get("about.donate");
        new AboutPanel(0, TITLE_H, W, H - TITLE_H, theme, fs,
                       title.c_str(), tag.c_str(), rows,
                       (donate && *donate && strcmp(donate, "about.donate") != 0) ? donate : "");

        end();
        finalizeShell();
    }

    ~AboutDialog() override = default;

    void centerAndShow() {
        position((Fl::w() - w()) / 2, (Fl::h() - h()) / 2);
        show();
    }
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
