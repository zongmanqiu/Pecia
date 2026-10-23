// DialogBase.cpp - Shared window shell for all Pecia dialogs & tool windows.
#include "ui/DialogBase.h"
#include "core/Theme.h"
#include "core/Config.h"
#include "ui/TitleBar.h"
#include "ui/WindowFrame.h"
#include "ui/WindowShadow.h"
#include "ui/NativeSubclass.h"
#include "ui/Layout.h"
#include "ToolChrome.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/x.H>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {
#if defined(_WIN32)
// Minimal native hook for modal dialogs: keep the outer glow glued to the
// host window's Z-order.
//
// Tool windows get this from ToolChrome's proc and the main window from
// MainWindow's, but a modal dialog has neither. FLTK raises the window right
// after show(), and the glow was inserted "behind the host" BEFORE that raise,
// so it stayed behind the main window: measured order became
//   关于 / Pecia / sh(Pecia) / sh(关于)
// instead of
//   关于 / sh(关于) / Pecia / sh(Pecia).
// With the glow under the main window, only the part hanging outside the main
// window was visible - which read as "the About dialog's shadow is missing /
// shows up late".
static LRESULT WINAPI dialogShadowProc(HWND hwnd, UINT msg, WPARAM wp,
                                       LPARAM lp) {
    switch (msg) {
    case WM_WINDOWPOSCHANGED:
    case WM_SHOWWINDOW: {
        LRESULT ret = nativeSubclass::forward(hwnd, dialogShadowProc, msg, wp, lp);
        windowShadow::sync(fl_find(hwnd));
        return ret;
    }
    case WM_NCACTIVATE:
        // 激活变化也同步一次阴影:tool/main 窗口都有各自的 NCACTIVATE 处理,
        // modal 原本没有——首次打开若 attach 期 z 序/显示竞态输给了 DWM,
        // 这里是事件驱动的自愈点(不依赖用户点击)。
        windowShadow::sync(fl_find(hwnd));
        break;
    case WM_DESTROY:
    case WM_NCDESTROY:
        windowShadow::detachHwnd(reinterpret_cast<void *>(hwnd));
        nativeSubclass::detachAll(hwnd);
        break;

    // Repaint the 1px border + arcs on top of the client area. draw() paints
    // them last, but draw() only runs when the WINDOW is damaged - a hover on
    // one of the dialog's buttons repaints that button alone and leaves the
    // arcs bitten out (measured 2026-10-22). Forwarding first means FLTK has
    // already blitted its offscreen buffer.
    case WM_PAINT: {
        LRESULT ret = nativeSubclass::forward(hwnd, dialogShadowProc, msg, wp, lp);
        Fl_Window *win = fl_find(hwnd);
        if (win)
            windowChrome::paintNonClientFrame(
                win, windowChrome::windowBorderColour(win));
        return ret;
    }
    }
    return nativeSubclass::forward(hwnd, dialogShadowProc, msg, wp, lp);
}
#endif

}  // namespace

DialogBase::DialogBase(int W, int H, const char *title,
                       const Theme *theme, int uiFontSize, Mode mode)
    : Fl_Double_Window(W, H, title)
    , m_theme(theme)
    , m_fontSize(uiFontSize ? uiFontSize : 16)
    , m_mode(mode) {
    commonCtor();
}

DialogBase::DialogBase(Config *cfg, int W, int H, const char *title,
                       int uiFontSize, Mode mode)
    : Fl_Double_Window(W, H, title)
    , m_fontSize(uiFontSize ? uiFontSize : 16)
    , m_mode(mode) {
    // Tool windows are standalone processes: load/own their Theme here so
    // the shared shell can color itself exactly like the caller's table.
    m_ownedTheme = new Theme();
    if (cfg) m_ownedTheme->load(*cfg);
    if (cfg) {
        windowChrome::setRoundedCornersEnabled(cfg->getRoundedCorners());
        // Tool windows are standalone processes: they own the outer-glow
        // switch too (the main window's View > Window Shadow reaches them
        // through the broadcast in ToolChrome).
        windowShadow::setEnabled(cfg->getWindowShadow());
    }
    theme_updateBorderColour(m_ownedTheme->colors());
    m_theme = m_ownedTheme;
    commonCtor();
}

void DialogBase::commonCtor() {
    // Draw our own frame (no OS title bar).
    border(0);
    box(FL_FLAT_BOX);
    color(m_theme ? m_theme->colors().background1 : FL_BACKGROUND2_COLOR);

    if (m_mode == ModalDialog) set_modal();
}

DialogBase::~DialogBase() {
    Fl::remove_timeout(fixToolTaskbarCb, this);
    Fl::remove_timeout(firstFrameFixCb, this);
    // Destroy the glow before the window does - a shadow whose owner is gone
    // would be left floating on the desktop.
    windowShadow::detach(this);
    delete m_frame;
    delete m_ownedTheme;   // nullptr in modal/dialog mode (external theme)
}

const Theme &DialogBase::retheme(const Config &cfg) {
    if (m_ownedTheme) {
        m_ownedTheme->load(cfg);
        theme_updateBorderColour(m_ownedTheme->colors());
        // Re-apply the shared shell palette (window bg + title bar).
        color(m_ownedTheme->colors().background1);
        if (m_titleBar) {
            m_titleBar->color(m_ownedTheme->colors().background2);
            m_titleBar->setTheme(m_ownedTheme);
        }
        // settings.ini is watched by the tool processes: keep rounded corners
        // in sync when the main window toggles View > Rounded Corners, and
        // refresh the DWM border colour after a theme change.
        windowChrome::setRoundedCornersEnabled(cfg.getRoundedCorners());
        windowChrome::applyRoundedCorners(
            this, cfg.getRoundedCorners(), m_ownedTheme->colors().text1);
        // The 1px ring is rasterised INTO the layered frame bitmap, and that
        // bitmap caches the colour it was built with. Rebuild it here, and
        // repaint the non-client line, or the window keeps the previous
        // theme's border/arc colour until it is moved (measured 2026-10-22:
        // the console's arc stayed white after switching to a light theme).
        windowShadow::sync(this);
        windowChrome::refreshFrameBand(this);
        // Changing round -> square leaves the old client-side arc pixels in
        // place; the tool/modal window only repaints its damaged parts, so
        // force one full redraw here (same residue as the main window's
        // View > Rounded Corners toggle).
        redraw();
        return *m_ownedTheme;
    }
    return *m_theme;   // modal dialog with external theme: nothing to reload
}

// One-shot follow-up for show(): unconditionally re-sync the outer glow once
// the event loop runs, and give modal dialogs their first activation.
void DialogBase::firstFrameFixCb(void *data) {
    auto *self = static_cast<DialogBase *>(data);
    if (!self || !self->shown()) return;
    windowShadow::sync(self);
#if defined(_WIN32)
    // SW_SHOWNOACTIVATE leaves a modal dialog unactivated; the first user
    // click was the only thing that ever activated it (and with it the first
    // WM_PAINT / WM_WINDOWPOSCHANGED storm that healed the frame). Activate
    // here instead, like the tool windows do in fixToolTaskbarCb.
    if (self->m_mode == ModalDialog) {
        if (HWND hwnd = fl_xid(self)) SetForegroundWindow(hwnd);
    }
#endif
}

void DialogBase::setCaptionButtons(int mask) {
    if (m_titleBar) m_titleBar->setEnabledButtons(mask);
}

void DialogBase::initShell(const char *title) {
    (void)title;
    // Create the shared custom title bar - identical to the main window's
    // TitleBar; only the visible caption buttons differ by mode.
    m_titleBar = new TitleBar(0, 0, w(), TITLE_H);
    m_titleBar->box(FL_FLAT_BOX);
    m_titleBar->color(m_theme ? m_theme->colors().background2 : FL_BACKGROUND2_COLOR);
    m_titleBar->setTheme(m_theme);
    m_titleBar->setFontSize(m_fontSize);

    if (m_mode == ToolWindow) {
        m_frame = new WindowFrame();
        setCaptionButtons(TitleBar::BTN_MASK_ALL);
    } else {
        setCaptionButtons(TitleBar::BTN_MASK_CLOSE_ONLY);
    }
}

void DialogBase::finalizeShell() {
    if (!m_titleBar) return;

    m_titleBar->setTabData({TitleBar::TabInfo{label() ? label() : "Pecia"}}, 0);
    m_titleBar->setMultiTab(false);

    m_titleBar->setOnTogglePin([this]() { onCaptionTogglePin(); });
    m_titleBar->setOnMinimize([this]() {
#if defined(_WIN32)
        HWND hwnd = fl_xid(this);
        if (hwnd) ShowWindow(hwnd, SW_MINIMIZE);
#else
        iconize();
#endif
    });
    m_titleBar->setOnToggleMaximize([this]() { onCaptionMaximize(); });
    m_titleBar->setOnCloseWindow([this]() { onCaptionClose(); });

    if (m_mode == ToolWindow) {
        // Keep the window resizable (subclass may call resizable(pane)
        // itself for a specific handle; otherwise the whole window works).
        if (!resizable()) resizable(this);
        m_titleBar->setEnabledButtons(TitleBar::BTN_MASK_ALL);
    } else {
        resizable(nullptr);   // fixed-size dialog
    }

    // Finalize the title bar size to the current window width. Some dialogs
    // resize the window (size()) after initShell() when computing an
    // adaptive width; this guarantees the close button is never outside the
    // window regardless of that ordering.
    if (m_titleBar) {
        m_titleBar->resize(0, 0, w(), TITLE_H);
    }
}

void DialogBase::onCaptionMaximize() {
#if defined(_WIN32)
    if (!m_frame) return;
    HWND hwnd = fl_xid(this);
    if (!hwnd) return;
    bool maxed = m_frame->toggleMaximize(this);
    if (m_titleBar) m_titleBar->setMaximized(maxed);
    windowChrome::refreshRoundedCorners(this);
#endif
}

void DialogBase::onCaptionTogglePin() {
#if defined(_WIN32)
    HWND hwnd = fl_xid(this);
    if (!hwnd) return;
    m_pinned = !m_pinned;
    SetWindowPos(hwnd, m_pinned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (m_titleBar) m_titleBar->setPinned(m_pinned);
#endif
}

void DialogBase::applyToolChrome() {
    if (m_mode == ToolWindow) {
        setupToolChrome(this, m_theme);
#if defined(_WIN32)
        // setupToolChrome adds WS_THICKFRAME / forces a frame change, so apply
        // the rounded-corner setting afterwards (DWM needs the final frame).
        windowChrome::applyRoundedCorners(
            this, windowChrome::roundedCornersEnabled(),
            m_theme ? m_theme->colors().text1 : FL_BLACK);
        // Deferred re-apply so the taskbar button appears even on the very
        // first launch (same race the main window solves in fixTaskbarCb).
        Fl::remove_timeout(fixToolTaskbarCb, this);
        Fl::add_timeout(0.25, fixToolTaskbarCb, this);
#endif
    }
}

// One-shot: re-apply WS_EX_APPWINDOW + activate so a tool window's taskbar
// button shows immediately. border(0) sets WS_EX_TOOLWINDOW which hides the
// window from the taskbar; on a first-launch race the one-shot apply in
// setupToolChrome can be lost before the taskbar sees a WM_ACTIVATE.
void DialogBase::fixToolTaskbarCb(void *data) {
    auto *self = static_cast<DialogBase *>(data);
    if (!self) return;
#if defined(_WIN32)
    HWND hwnd = fl_xid(self);
    if (!hwnd) return;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ex &= ~WS_EX_TOOLWINDOW;
    ex |= WS_EX_APPWINDOW;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);
    windowChrome::refreshRoundedCorners(self);
    SetForegroundWindow(hwnd);
#endif
}

void DialogBase::draw() {
    Fl_Double_Window::draw();
    // Modal dialogs draw their own 1px border here (they have no native
    // non-client frame). Tool windows instead get left/right/bottom from the
    // WM_NCPAINT handler (setupToolChrome) - only their top line lives on
    // client row 0, where every client repaint wipes it.
    Fl_Color bc = m_theme ? m_theme->colors().text1 : fl_rgb_color(127, 127, 127);
    const bool rounded = windowChrome::roundedCornersActive(this);
    const int rad = rounded ? windowChrome::roundedCornerRadiusPx(this) : 0;
    int padX = 0, padY = 0;
#if defined(_WIN32)
    windowChrome::frameClientOrigin(this, &padX, &padY);
#endif
    // The arcs live in WINDOW coordinates; FLTK draws in CLIENT coordinates,
    // and the client origin is inset by NC_PAD on the left for tool windows.
    // Mixing the two used to leave a gap between the arc and the straight
    // line (measured 2026-10-22, temp/hc_main_rounded1_full.png).
    const int x0 = rounded ? rad - padX : 0;
    const int xlen = rounded ? (w() + 2 * padX - 2 * rad) : w();

    ::fl_color(bc);
    if (xlen > 0) ::fl_rectf(x0, 0, xlen, 1);            // top edge, always here
    if (m_mode == ModalDialog) {
        const int y0 = rounded ? rad : 0;
        const int ylen = rounded ? (h() - 2 * rad) : h();
        if (xlen > 0) ::fl_rectf(x0, h() - 1, xlen, 1);
        if (ylen > 0) {
            ::fl_rectf(0, y0, 1, ylen);
            ::fl_rectf(w() - 1, y0, 1, ylen);
        }
    }
    if (!rounded) return;
    // Anti-aliased corner arcs, blended against whatever is underneath. The NC
    // painter draws the same pixels for the non-client halves; everything it
    // touches inside the client rect is erased by the next repaint, so they
    // are redrawn here (identical colours - the blend is stateless).
    static windowChrome::FramePixel pix[1024];
    const int n = windowChrome::buildCornerStroke(
        this, (unsigned int)bc, padX, padY, pix, 1024);
    for (int i = 0; i < n; ++i) {
        ::fl_color((Fl_Color)pix[i].colour);
        ::fl_rectf(pix[i].x, pix[i].y, 1, 1);
    }
}

void DialogBase::show() {
    // NOTE: the glow cannot be prepared before this point. FLTK only creates
    // the native window inside Fl_Window::show(), so attach() called earlier
    // gets a null fl_xid() and quietly does nothing (measured: doing it
    // "early" made the About dialog come up with NO glow at all).
    Fl_Double_Window::show();
#if defined(_WIN32)
    // Modal dialogs have no ToolChrome proc; apply the shared rounded-corner
    // setting as soon as the HWND exists. Tool windows get re-applied again
    // in applyToolChrome() after WS_THICKFRAME is installed.
    windowChrome::applyRoundedCorners(
        this, windowChrome::roundedCornersEnabled(),
        m_theme ? m_theme->colors().text1 : FL_BLACK);
    // Hook the native proc so the glow follows the host's Z-order (FLTK
    // raises the window just after show()). Tool windows already have
    // ToolChrome's proc, so only modal dialogs need this one.
    if (m_mode == ModalDialog) {
        if (HWND h = fl_xid(this)) nativeSubclass::install(h, dialogShadowProc);
    }
    // Same outer glow as every other Pecia window: create + build the ramp +
    // put it on screen in one call, so nothing is left for a later frame.
    windowShadow::attach(this);
    // ── 首帧装饰就位(修「圆角/阴影/边框偶发缺失,点一下标题栏才恢复」)──
    // Fl_Window::show() 内部已经 ShowWindow(SW_SHOWNOACTIVATE) 上屏,而上面三步
    // (region / proc / shadow)全部发生在「窗口已可见」之后;DWM 按自己的节奏
    // 合成,不等这条线程。首帧若晚到,modal 的修正事件(WM_SHOWWINDOW /
    // WM_WINDOWPOSCHANGED / 首个 WM_PAINT 钩子)一个也没接住——直到用户第一次
    // 点击标题栏(首次激活带来第一波 WM_PAINT/WM_WINDOWPOSCHANGED)才恢复。
    // 这里:1) RDW_UPDATENOW 在 show() 返回前同步派发 WM_PAINT,把 1px 边框 +
    // 圆角弧线画在第一次 DWM 合成之前(与工具窗 refreshFrameBand 同参数);
    // 2) 一次性 0s timeout 兑底:下一轮泵消息时无条件重 sync 阴影(治愈 attach
    // 期被错过的 z 序/可见性竞态),并给 modal 补上首激活。析构里已 remove。
    if (HWND h = fl_xid(this)) {
        RedrawWindow(h, nullptr, nullptr,
                     RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
    }
    // 双段自愈:0s(sync + 补首激活)与 0.25s(再 sync + 再强制重绘)各一次。
    // attach 期与 DWM 的首次合成存在窗口期,单次兜底仍可能输掉;第二段保证
    // 最多 0.25s 内无交互自愈,不再依赖用户点击。析构里统一 remove。
    Fl::remove_timeout(firstFrameFixCb, this);
    Fl::add_timeout(0.0, firstFrameFixCb, this);
    Fl::add_timeout(0.25, firstFrameFixCb, this);
#endif
}

void DialogBase::hide() {
#if defined(_WIN32)
    // Destroy the glow BEFORE hiding. FLTK's Fl_Window::hide() tears down the
    // native window too (fl_xid() returns null afterwards), so bookkeeping
    // done after it silently does nothing - that is why the glow used to
    // survive a closed dialog: both the sync() and the detach() attempted
    // after hide() were no-ops on a null HWND.
    // A modal dialog is either destroyed (stack object) or re-shown, and
    // show() creates a fresh glow, so destroying here costs nothing.
    windowShadow::detach(this);
#endif
    Fl_Double_Window::hide();
}

int DialogBase::handle(int event) {
    // Route title-bar-affecting events (move/hover, drag, push, release) to
    // the TitleBar, exactly like the tool windows did before this base.
    if (event == FL_MOVE || event == FL_PUSH || event == FL_DRAG ||
        event == FL_RELEASE || event == FL_LEAVE) {
        if (m_titleBar) {
            int mx = Fl::event_x();
            int my = Fl::event_y();
            bool inTitleBar = (my >= 0 && my < TITLE_H && mx >= 0 && mx < w());
            if (event == FL_DRAG || event == FL_RELEASE) {
                // Only forward DRAG/RELEASE to TitleBar if the original FL_PUSH
                // started inside the title bar (title-bar drag is active).
                // Otherwise the event belongs to a child widget (e.g. UrlWidget
                // click) and must not be consumed by the title bar.
                if (m_titleBarDragging) {
                    int ret = m_titleBar->handle(event);
                    if (event == FL_RELEASE) m_titleBarDragging = false;
                    if (ret) return ret;
                }
            } else if (inTitleBar) {
                int ret = m_titleBar->handle(event);
                if (ret) {
                    if (event == FL_PUSH) m_titleBarDragging = true;
                    return ret;
                }
            } else if (event == FL_MOVE) {
                m_titleBar->handle(FL_LEAVE);
            }
        }
    }
    return Fl_Double_Window::handle(event);
}

void DialogBase::resize(int X, int Y, int W, int H) {
    Fl_Double_Window::resize(X, Y, W, H);
    // Keep the shared title bar covering the full window width. Dialogs
    // that compute their width inside the constructor (e.g. ParamDialog)
    // resize the window after initShell(); without this the title bar's
    // close button would stay at the pre-resize width and stick out.
    if (m_titleBar) {
        m_titleBar->resize(0, 0, w(), TITLE_H);
        m_titleBar->redraw();
    }
    // Rebuild the Win10 fallback region for the new size (no-op on Win11).
    windowChrome::refreshRoundedCorners(this);
    // Follow the new geometry (also rebuilds the ramp, whose size changed).
    windowShadow::sync(this);
}
