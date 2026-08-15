// DialogBase.cpp - Shared window shell for all Pecia dialogs & tool windows.
#include "ui/DialogBase.h"
#include "core/Theme.h"
#include "core/Config.h"
#include "ui/TitleBar.h"
#include "ui/WindowFrame.h"
#include "ui/Layout.h"
#include "ToolChrome.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/x.H>

#if defined(_WIN32)
#include <windows.h>
#endif

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
    m_theme = m_ownedTheme;
    commonCtor();
}

void DialogBase::commonCtor() {
    // Draw our own frame (no OS title bar).
    border(0);
    box(FL_FLAT_BOX);
    color(m_theme ? m_theme->colors().bgEditor : FL_BACKGROUND2_COLOR);

    if (m_mode == ModalDialog) set_modal();
}

DialogBase::~DialogBase() {
    delete m_frame;
    delete m_ownedTheme;   // nullptr in modal/dialog mode (external theme)
}

const Theme &DialogBase::retheme(const Config &cfg) {
    if (m_ownedTheme) {
        m_ownedTheme->load(cfg);
        // Re-apply the shared shell palette (window bg + title bar).
        color(m_ownedTheme->colors().bgEditor);
        if (m_titleBar) {
            m_titleBar->color(m_ownedTheme->colors().bgChrome);
            m_titleBar->setTheme(m_ownedTheme);
        }
        return *m_ownedTheme;
    }
    return *m_theme;   // modal dialog with external theme: nothing to reload
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
    m_titleBar->color(m_theme ? m_theme->colors().bgChrome : FL_BACKGROUND2_COLOR);
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
    if (m_mode == ToolWindow) setupToolChrome(this, m_theme);
}

void DialogBase::draw() {
    Fl_Double_Window::draw();
    // Modal dialogs draw their own 1px border here (they have no native
    // non-client frame). Tool windows instead get their 1px border from the
    // WM_NCPAINT handler (setupToolChrome), again as exactly one line.
    if (m_mode != ModalDialog) return;
    Fl_Color bc = m_theme ? m_theme->colors().borderColor : fl_rgb_color(127, 127, 127);
    ::fl_color(bc);
    ::fl_rectf(0, 0, w(), 1);
    ::fl_rectf(0, h() - 1, w(), 1);
    ::fl_rectf(0, 0, 1, h());
    ::fl_rectf(w() - 1, 0, 1, h());
}

int DialogBase::handle(int event) {
    // Route title-bar-affecting events (move/hover, drag, push, release) to
    // the TitleBar, exactly like the tool windows did before this base.
    if (event == FL_MOVE || event == FL_PUSH || event == FL_DRAG ||
        event == FL_RELEASE || event == FL_LEAVE) {
        if (m_titleBar) {
            int mx = Fl::event_x();
            int my = Fl::event_y();
            if (event == FL_DRAG || event == FL_RELEASE) {
                int ret = m_titleBar->handle(event);
                if (ret) return ret;
            } else if (my >= 0 && my < TITLE_H && mx >= 0 && mx < w()) {
                int ret = m_titleBar->handle(event);
                if (ret) return ret;
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
}
