// AboutDialog.cpp - "About Pecia" dialog.
// Built with the same window/layout conventions as SettingsDialog (Options):
// a title bar created via the shared factory, content rows laid out with
// plain Fl_Box widgets in a fixed label column (clean vertical alignment),
// and no bottom button bar. QR images load from the exe-adjacent image/
// folder (donate_wechat.png / donate_alipay.png); a placeholder box is drawn
// if a file is missing so the dialog always works.
#include "ui/AboutDialog.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "ui/InfoWindow.h"      // createInfoTitleBar factory
#include "ui/Layout.h"          // TITLE_H, gBarH, gBtnH, fonts

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

#include <cstring>
#include <vector>

namespace {

// Border-drawing window subclass: paints the outer frame ON TOP of all
// children so the border never disappears (same as the sharing dialogs).
class AboutWin : public Fl_Double_Window {
    const Theme *m_theme;
public:
    AboutWin(int W, int H, const char *title, const Theme *theme)
        : Fl_Double_Window(W, H, title), m_theme(theme) {}
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

// Clickable URL Fl_Box: opens the link on left-click; underlines on hover.
class LinkBox : public Fl_Box {
    const char *m_url;
    bool m_hover = false;
public:
    LinkBox(int X, int Y, int W, int H, const char *url, const char *text,
            int fs, Fl_Color c)
        : Fl_Box(X, Y, W, H, text), m_url(url) {
        box(FL_NO_BOX);
        labelsize(fs);
        labelcolor(c);
        align(FL_ALIGN_LEFT | FL_ALIGN_CENTER);
    }
    void draw() FL_OVERRIDE {
        Fl_Box::draw();
        if (m_hover) {
            int tw = (int)fl_width(label());
            fl_color(labelcolor());
            fl_line(x(), y() + h() / 2 + fl_descent() - 1,
                    x() + tw, y() + h() / 2 + fl_descent() - 1);
        }
    }
    int handle(int event) FL_OVERRIDE {
        if (event == FL_ENTER) { m_hover = true; redraw(); fl_cursor(FL_CURSOR_HAND); return 1; }
        if (event == FL_LEAVE) { m_hover = false; redraw(); fl_cursor(FL_CURSOR_DEFAULT); return 1; }
        if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE) {
            if (m_url && *m_url) ShellExecuteA(nullptr, "open", m_url, nullptr, nullptr, SW_SHOWNORMAL);
            return 1;
        }
        return 0;
    }
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

} // namespace

void showAboutDialog(const Theme *theme, int uiFontSize) {
    if (!theme) return;

    const int fs = uiFontSize ? uiFontSize : 14;
    const int W  = 500;
    const int H  = 600;
    const int margin = 20;
    const int labelW = 120;   // fixed label column (right-aligned)
    int textCol = margin + labelW + 10;   // value column starts here

    AboutWin dlg(W, H, "About", theme);
    dlg.border(0);
    dlg.box(FL_FLAT_BOX);
    dlg.color(theme->colors().bgEditor);
    dlg.set_modal();
    dlg.begin();

    createInfoTitleBar(0, 0, W, TITLE_H, I18n::get("menu.help.about"), theme, fs);

    // ── Header: large app title + tagline ──
    Fl_Box *appTitle = new Fl_Box(0, TITLE_H + 20, W, 42);
    appTitle->box(FL_NO_BOX);
    appTitle->labelsize(fs + 14);
    appTitle->labelfont(FL_HELVETICA_BOLD);
    appTitle->labelcolor(theme->colors().textPrimary);
    appTitle->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    const char *appName = I18n::get("app.name");
    appTitle->label(appName && *appName && strcmp(appName, "app.name") != 0
                        ? appName : "Pecia 1.0.0");

    Fl_Box *tagline = new Fl_Box(0, TITLE_H + 62, W, 24);
    tagline->box(FL_NO_BOX);
    tagline->labelsize(fs);
    tagline->labelcolor(theme->colors().textSecondary);
    tagline->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    const char *tag = I18n::get("about.tagline");
    tagline->label(tag && *tag && strcmp(tag, "about.tagline") != 0 ? tag : "");

    // ── Info rows: fixed label column + value column (plain Fl_Box) ──
    int y = TITLE_H + 112;
    const int ROW_H = 28;
    Fl_Color lblCol = theme->colors().textSecondary;
    Fl_Color valCol = theme->colors().textPrimary;

    auto row = [&](const char *label, const char *value) {
        Fl_Box *lb = new Fl_Box(margin, y, labelW, ROW_H, label);
        lb->box(FL_NO_BOX);
        lb->labelsize(fs);
        lb->labelcolor(lblCol);
        lb->align(FL_ALIGN_RIGHT | FL_ALIGN_CENTER);
        Fl_Box *vb = new Fl_Box(textCol, y, W - textCol - margin, ROW_H, value);
        vb->box(FL_NO_BOX);
        vb->labelsize(fs);
        vb->labelcolor(valCol);
        vb->align(FL_ALIGN_LEFT | FL_ALIGN_CENTER);
        y += ROW_H;
    };

    row(I18n::get("about.author"), "邱宗满 (Qiu Zongman)");
    row(I18n::get("about.email"), "qiuzongman@foxmail.com");

    // Project: label column + clickable link (same columns as the rows).
    {
        Fl_Box *lb = new Fl_Box(margin, y, labelW, ROW_H, I18n::get("about.project"));
        lb->box(FL_NO_BOX);
        lb->labelsize(fs);
        lb->labelcolor(lblCol);
        lb->align(FL_ALIGN_RIGHT | FL_ALIGN_CENTER);
        new LinkBox(textCol, y, W - textCol - margin, ROW_H,
                    "https://gitee.com/qiuzongman/pecia",
                    "https://gitee.com/qiuzongman/pecia",
                    fs, theme->colors().linkHover);
        y += ROW_H;
    }
    row(I18n::get("about.license"), "AGPL-3.0");
    row(I18n::get("about.ai"), "DeepSeek");

    y += 6;

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
            int dw = QR_SZ, dh = QR_SZ;
            if (img->w() > 0 && img->h() > 0) {
                float sc = (float)QR_SZ / (img->w() > img->h() ? img->w() : img->h());
                if (sc > 1.0f) sc = 1.0f;
                dw = (int)(img->w() * sc);
                dh = (int)(img->h() * sc);
            }
            Fl_Box *pic = new Fl_Box(qx + (QR_SZ - dw) / 2, y + 26, dw, dh);
            pic->image(img);
            pic->box(FL_DOWN_BOX);
        }
    }
    y += 26 + QR_SZ + 12;

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
