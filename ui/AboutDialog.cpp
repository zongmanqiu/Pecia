// AboutDialog.cpp - "About Pecia" dialog.
// Shows version, author, email, clickable project link, license, AI tool,
// and a donation card with two QR codes. QR images are loaded from the
// exe-adjacent image/ folder (donate_wechat.png / donate_alipay.png); a
// placeholder box is drawn if a file is missing so the dialog always works.
#include "ui/AboutDialog.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "ui/InfoWindow.h"      // InfoTitleBar / BorderOverlay helpers
#include "core/Config.h"        // exeDir via Config

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#if defined(_WIN32)
#include <shellapi.h>
#include <windows.h>
#endif

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr int kTitleH = 36;

// ---------------------------------------------------------------------------
// ClickableLink - a label that opens a URL when clicked/pressed (and shows
// a hand cursor on hover). Matches the project's URL-open style.
// ---------------------------------------------------------------------------
class ClickableLink : public Fl_Widget {
    const char *m_url;
    const char *m_text;
    const Theme *m_theme;
    int  m_fontSize;
    bool m_hover = false;
public:
    ClickableLink(int X, int Y, int W, int H, const char *url,
                  const char *text, const Theme *theme, int fs)
        : Fl_Widget(X, Y, W, H), m_url(url), m_text(text),
          m_theme(theme), m_fontSize(fs) {}

    void draw() FL_OVERRIDE {
        Fl_Color c = m_theme ? m_theme->colors().linkHover
                             : fl_rgb_color(6, 69, 173);
        fl_color(c);
        fl_font(FL_HELVETICA, m_fontSize);
        int tw = (int)fl_width(m_text);
        fl_draw(m_text, x(), y(), w(), h(), FL_ALIGN_LEFT | FL_ALIGN_CENTER);
        // underline
        int ty = y() + h() / 2 + fl_descent() - 1;
        if (m_hover) {
            fl_line(x(), ty, x() + tw, ty);
        }
    }

    int handle(int event) FL_OVERRIDE {
        switch (event) {
        case FL_ENTER: m_hover = true;  redraw();
            // hand cursor
            fl_cursor(FL_CURSOR_HAND); return 1;
        case FL_LEAVE: m_hover = false; redraw();
            fl_cursor(FL_CURSOR_DEFAULT); return 1;
        case FL_PUSH:
            if (Fl::event_button() == FL_LEFT_MOUSE) {
                if (m_url && *m_url)
                    ShellExecuteA(nullptr, "open", m_url, nullptr, nullptr, SW_SHOWNORMAL);
                return 1;
            }
            return 0;
        default: return 0;
        }
    }
};

// ---------------------------------------------------------------------------
// AboutRow - "label  value" line (right-aligned label, left value).
// ---------------------------------------------------------------------------
class AboutRow : public Fl_Widget {
    const char *m_label;
    const char *m_value;
    const Theme *m_theme;
    int m_fontSize;
public:
    AboutRow(int X, int Y, int W, int H, const char *label, const char *value,
             const Theme *theme, int fs)
        : Fl_Widget(X, Y, W, H), m_label(label), m_value(value),
          m_theme(theme), m_fontSize(fs) {}

    void draw() FL_OVERRIDE {
        Fl_Color bg = m_theme ? m_theme->colors().bgEditor : FL_WHITE;
        fl_draw_box(FL_FLAT_BOX, x(), y(), w(), h(), bg);
        Fl_Color labelCol = m_theme ? m_theme->colors().textSecondary
                                    : fl_rgb_color(120, 120, 120);
        Fl_Color valCol   = m_theme ? m_theme->colors().textPrimary : FL_BLACK;
        fl_font(FL_HELVETICA, m_fontSize);
        fl_color(labelCol);
        fl_draw(m_label, x() + 16, y(), 110, h(), FL_ALIGN_RIGHT | FL_ALIGN_CENTER);
        fl_color(valCol);
        fl_draw(m_value, x() + 130, y(), w() - 146, h(), FL_ALIGN_LEFT | FL_ALIGN_CENTER);
    }
    int handle(int) FL_OVERRIDE { return 0; }
};

// Load exeDir/image/<name> as a Fl_Image*. Returns nullptr if unavailable.
Fl_Image *loadImage(const char *name, const Theme *theme) {
    wchar_t exe[MAX_PATH];
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return nullptr;
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (!slash) return nullptr;
    *(slash + 1) = 0;
    swprintf(path, MAX_PATH, L"%simage\\%hs", exe, name);
    // Fl_PNG_Image expects the OS codepage/narrow path in 1.4; use the wide
    // file check then load via a valid narrow path.
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return nullptr;
    char narrow[MAX_PATH * 2];
    int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, (int)sizeof(narrow),
                                nullptr, nullptr);
    if (n <= 0) return nullptr;
    (void)theme;
    Fl_PNG_Image *img = new Fl_PNG_Image(narrow);
    if (img->w() <= 0) { delete img; return nullptr; }
    return img;
}

// Draw a simple placeholder for a missing QR (grey rounded-ish square with
// a caption) so the layout is never broken before real images are dropped in.
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
    Fl_Image *img = surf.image();
    (void)caption;
    return img;
}

} // namespace

void showAboutDialog(const Theme *theme, int uiFontSize) {
    if (!theme) return;

    const int fs = uiFontSize ? uiFontSize : 14;
    const int W  = 480;
    const int H  = 572;

    Fl_Double_Window dlg(W, H);
    dlg.border(0);
    dlg.box(FL_FLAT_BOX);
    dlg.color(theme->colors().bgEditor);
    dlg.set_modal();

    // InfoTitleBar/BorderOverlay are forward-declared (incomplete); the
    // factory functions create and theme them in place as window children.
    createInfoTitleBar(0, 0, W, kTitleH, I18n::get("menu.help.about"), theme, fs);

    // Large title
    Fl_Box *title = new Fl_Box(0, kTitleH + 16, W, 44);
    title->box(FL_NO_BOX);
    title->labelsize(fs + 12);
    title->labelcolor(theme->colors().textPrimary);
    title->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    title->label("Pecia 1.0.0");
    title->labelfont(FL_HELVETICA_BOLD);

    Fl_Box *sub = new Fl_Box(0, kTitleH + 58, W, 22);
    sub->box(FL_NO_BOX);
    sub->labelsize(fs);
    sub->labelcolor(theme->colors().textSecondary);
    sub->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    sub->label(I18n::get("about.tagline"));

    // Info rows
    int y = kTitleH + 100;
    const int ROW_H = 30;
    new AboutRow(0, y, W, ROW_H, I18n::get("about.author"),
                 "邱宗满 (Qiu Zongman)", theme, fs); y += ROW_H;
    new AboutRow(0, y, W, ROW_H, I18n::get("about.email"),
                 "qiuzongman@foxmail.com", theme, fs); y += ROW_H;
    // 项目地址 - clickable link
    Fl_Box *projLabel = new Fl_Box(16, y, 110, ROW_H, I18n::get("about.project"));
    projLabel->box(FL_NO_BOX);
    projLabel->labelsize(fs);
    projLabel->labelcolor(theme->colors().textSecondary);
    projLabel->align(FL_ALIGN_RIGHT | FL_ALIGN_CENTER);
    new ClickableLink(146, y, W - 162, ROW_H,
                      "https://gitee.com/qiuzongman/pecia",
                      "https://gitee.com/qiuzongman/pecia",
                      theme, fs); y += ROW_H;
    new AboutRow(0, y, W, ROW_H, I18n::get("about.license"),
                 "AGPL-3.0", theme, fs); y += ROW_H;
    new AboutRow(0, y, W, ROW_H, I18n::get("about.ai"),
                 "DeepSeek", theme, fs); y += ROW_H + 8;

    // Donate
    Fl_Box *donateTitle = new Fl_Box(0, y, W, 26, I18n::get("about.donate"));
    donateTitle->box(FL_NO_BOX);
    donateTitle->labelsize(fs);
    donateTitle->labelcolor(theme->colors().textPrimary);
    donateTitle->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    y += 32;

    // Two QR images (side by side)
    const int QR_SZ = 200;
    const int gap = 16;
    int qrY = y;
    std::vector<Fl_Image *> ownedImages;   // freed before the dialog closes
    const char *labels[2] = { "微信 / WeChat", "支付宝 / Alipay" };
    const char *files[2] = { "donate_wechat.png", "donate_alipay.png" };
    for (int i = 0; i < 2; ++i) {
        int qx = (i == 0) ? (W / 2 - gap - QR_SZ) : (W / 2 + gap);
        Fl_Box *cap = new Fl_Box(qx, qrY, QR_SZ, 20, labels[i]);
        cap->box(FL_NO_BOX);
        cap->labelsize(fs - 2);
        cap->labelcolor(theme->colors().textSecondary);
        cap->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);

        Fl_Image *img = loadImage(files[i], theme);
        if (!img) img = placeholderImage(labels[i]);
        if (img) {
            ownedImages.push_back(img);
            int iw = img->w() > 0 ? img->w() : QR_SZ;
            int ih = img->h() > 0 ? img->h() : QR_SZ;
            // Fit into QR_SZ box preserving aspect
            float scale = (float)QR_SZ / (iw > ih ? iw : ih);
            if (scale > 1.0f) scale = 1.0f;
            int dw = (int)(iw * scale), dh = (int)(ih * scale);
            Fl_Box *pic = new Fl_Box(qx + (QR_SZ - dw) / 2, qrY + 24, dw, dh);
            pic->image(img);
            pic->box(FL_DOWN_BOX);
        }
    }

    createBorderOverlay(0, kTitleH, W, H - kTitleH, theme);

    dlg.end();
    dlg.position((Fl::w() - W) / 2, (Fl::h() - H) / 2);
    dlg.show();
#if defined(_WIN32)
    HWND hwnd = fl_xid(&dlg);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_APPWINDOW);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
#endif
    while (dlg.shown()) Fl::wait();
    for (Fl_Image *im : ownedImages) delete im;
}
