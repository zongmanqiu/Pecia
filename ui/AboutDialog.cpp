// AboutDialog.cpp - "About Pecia" dialog.
// Built the same way as the other dialogs (SettingsDialog / InfoWindow):
// an Fl_Double_Window subclass that draws the outer border on top of all
// children (so the border never disappears), a themed InfoTitleBar via the
// shared factory, and label/value rows with a fixed label column for clean
// alignment. QR images load from the exe-adjacent image/ folder
// (donate_wechat.png / donate_alipay.png); a placeholder box is drawn if a
// file is missing so the dialog always works.
#include "ui/AboutDialog.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "ui/InfoWindow.h"      // createInfoTitleBar factory

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#if defined(_WIN32)
#include <shellapi.h>
#include <windows.h>
#endif

#include <cstring>
#include <vector>

namespace {

constexpr int kTitleH = 36;
constexpr int kLabelW = 120;   // fixed label column width (right-aligned)

// ---------------------------------------------------------------------------
// AboutDialogWin - Fl_Double_Window subclass whose draw() paints the outer
// border last, on top of every child, so the frame is always visible (same
// approach as InfoDialog / SettingsDialog's ExtensionsDialog).
// ---------------------------------------------------------------------------
class AboutDialogWin : public Fl_Double_Window {
    const Theme *m_theme;
public:
    AboutDialogWin(int W, int H, const Theme *theme)
        : Fl_Double_Window(W, H), m_theme(theme) {}
    void draw() FL_OVERRIDE {
        Fl_Double_Window::draw();
        Fl_Color bc = m_theme ? m_theme->colors().borderColor
                              : fl_rgb_color(127, 127, 127);
        ::fl_color(bc);
        ::fl_rectf(0, 0, w(), 1);
        ::fl_rectf(0, h() - 1, w(), 1);
        ::fl_rectf(0, 0, 1, h());
        ::fl_rectf(w() - 1, 0, 1, h());
    }
};

// A clickable URL text (underlined on hover); opens via ShellExecuteA.
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
        if (m_hover) {   // underline on hover only
            int ty = y() + h() / 2 + fl_descent() - 1;
            fl_line(x(), ty, x() + tw, ty);
        }
    }
    int handle(int event) FL_OVERRIDE {
        switch (event) {
        case FL_ENTER: m_hover = true; redraw(); fl_cursor(FL_CURSOR_HAND); return 1;
        case FL_LEAVE: m_hover = false; redraw(); fl_cursor(FL_CURSOR_DEFAULT); return 1;
        case FL_PUSH:
            if (Fl::event_button() == FL_LEFT_MOUSE && m_url && *m_url) {
                ShellExecuteA(nullptr, "open", m_url, nullptr, nullptr, SW_SHOWNORMAL);
                return 1;
            }
            return 0;
        default: return 0;
        }
    }
};

// ---------------------------------------------------------------------------
// A label row: fixed-width right-aligned label column + value. Using the same
// kLabelW for every row keeps the labels vertically aligned.
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
        Fl_Color labelCol = m_theme ? m_theme->colors().textSecondary
                                    : fl_rgb_color(110, 110, 110);
        Fl_Color valCol   = m_theme ? m_theme->colors().textPrimary : FL_BLACK;
        fl_font(FL_HELVETICA, m_fontSize);
        // fixed label column (x()..x()+kLabelW), value to its right
        fl_color(labelCol);
        fl_draw(m_label, x(), y(), kLabelW, h(),
                FL_ALIGN_RIGHT | FL_ALIGN_CENTER);
        fl_color(valCol);
        fl_draw(m_value, x() + kLabelW + 6, y(), w() - kLabelW - 6, h(),
                FL_ALIGN_LEFT | FL_ALIGN_CENTER);
    }
    int handle(int) FL_OVERRIDE { return 0; }
};

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

// Placeholder for a missing QR image so the layout is never broken.
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

} // namespace

void showAboutDialog(const Theme *theme, int uiFontSize) {
    if (!theme) return;

    const int fs = uiFontSize ? uiFontSize : 14;
    const int W  = 500;
    const int H  = 580;
    const int margin = 24;

    AboutDialogWin dlg(W, H, theme);
    dlg.border(0);
    dlg.box(FL_FLAT_BOX);
    dlg.color(theme->colors().bgEditor);
    dlg.set_modal();
    dlg.begin();   // begin adding children as window children

    // Shared factory builds (and themes) the title bar in place.
    createInfoTitleBar(0, 0, W, kTitleH, I18n::get("menu.help.about"), theme, fs);

    // ── Header: large app title ──
    Fl_Box *appTitle = new Fl_Box(0, kTitleH + 20, W, 40);
    appTitle->box(FL_NO_BOX);
    appTitle->labelsize(fs + 14);
    appTitle->labelfont(FL_HELVETICA_BOLD);
    appTitle->labelcolor(theme->colors().textPrimary);
    appTitle->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    const char *appName = I18n::get("app.name");
    appTitle->label(appName && *appName && strcmp(appName, "app.name") != 0
                        ? appName : "Pecia 1.0.0");

    Fl_Box *tagline = new Fl_Box(0, kTitleH + 62, W, 24);
    tagline->box(FL_NO_BOX);
    tagline->labelsize(fs);
    tagline->labelcolor(theme->colors().textSecondary);
    tagline->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    const char *tag = I18n::get("about.tagline");
    tagline->label(tag && *tag && strcmp(tag, "about.tagline") != 0 ? tag : "");

    // ── info rows (fixed label column for clean alignment) ──
    int y = kTitleH + 110;
    const int ROW_H = 30;
    new AboutRow(margin, y, W - 2 * margin, ROW_H, I18n::get("about.author"),
                 "邱宗满 (Qiu Zongman)", theme, fs); y += ROW_H;
    new AboutRow(margin, y, W - 2 * margin, ROW_H, I18n::get("about.email"),
                 "qiuzongman@foxmail.com", theme, fs); y += ROW_H;

    // Project: label column + clickable link aligned to the value column.
    {
        Fl_Box *lbl = new Fl_Box(margin, y, kLabelW, ROW_H, I18n::get("about.project"));
        lbl->box(FL_NO_BOX);
        lbl->labelsize(fs);
        lbl->labelcolor(theme->colors().textSecondary);
        lbl->align(FL_ALIGN_RIGHT | FL_ALIGN_CENTER);
        new ClickableLink(margin + kLabelW + 6, y, W - 2 * margin - kLabelW - 6,
                          ROW_H, "https://gitee.com/qiuzongman/pecia",
                          "https://gitee.com/qiuzongman/pecia", theme, fs);
        y += ROW_H;
    }
    new AboutRow(margin, y, W - 2 * margin, ROW_H, I18n::get("about.license"),
                 "AGPL-3.0", theme, fs); y += ROW_H;
    new AboutRow(margin, y, W - 2 * margin, ROW_H, I18n::get("about.ai"),
                 "DeepSeek", theme, fs); y += ROW_H + 4;

    // ── Donate title ──
    Fl_Box *donate = new Fl_Box(0, y, W, 26, I18n::get("about.donate"));
    donate->box(FL_NO_BOX);
    donate->labelsize(fs);
    donate->labelcolor(theme->colors().textPrimary);
    donate->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    y += 30;

    // ── Two QR images (side by side, centered) ──
    const int QR_SZ = 196;
    const int gap = 14;
    std::vector<Fl_Image *> ownedImages;
    const char *labels[2] = { "微信 / WeChat", "支付宝 / Alipay" };
    const char *files[2] = { "donate_wechat.png", "donate_alipay.png" };
    int rowW = 2 * QR_SZ + gap;
    int startX = (W - rowW) / 2;
    for (int i = 0; i < 2; ++i) {
        int qx = startX + i * (QR_SZ + gap);
        Fl_Box *cap = new Fl_Box(qx, y, QR_SZ, 22, labels[i]);
        cap->box(FL_NO_BOX);
        cap->labelsize(fs - 2);
        cap->labelcolor(theme->colors().textSecondary);
        cap->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);

        Fl_Image *img = loadImage(files[i]);
        if (!img) img = placeholderImage(labels[i]);
        if (img) {
            ownedImages.push_back(img);
            int iw = img->w() > 0 ? img->w() : QR_SZ;
            int ih = img->h() > 0 ? img->h() : QR_SZ;
            float scale = (float)QR_SZ / (iw > ih ? iw : ih);
            if (scale > 1.0f) scale = 1.0f;
            int dw = (int)(iw * scale), dh = (int)(ih * scale);
            Fl_Box *pic = new Fl_Box(qx + (QR_SZ - dw) / 2, y + 26, dw, dh);
            pic->image(img);
            pic->box(FL_DOWN_BOX);
        }
    }
    y += 26 + QR_SZ + 10;

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
