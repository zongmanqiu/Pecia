// DialogBase.h - Shared base for every Pecia window / dialog.
//
// Every modal dialog (Options, About, Confirm, Param, Shortcut, AI config,
// ...) AND the standalone tool windows (AI chat, Lua console) derive from
// this one class, so they all share the same window skeleton:
//
//   - custom title bar drawn with the same TitleBar (four caption buttons,
//     reduced to just close for simple dialogs via setEnabledButtons)
//   - the same 1px outer border / frame behavior
//   - the same color scheme from Theme
//
// A "mode" selects between a static modal dialog (fixed size, window-managed
// off) and a standalone tool window (resizable + WindowFrame + native chrome).
//
// Usage from a subclass constructor:
//     begin();                       // Fl_Window::begin()
//     initShell(title);              // creates the shared TitleBar (+ frame in tool mode)
//     ... add your content widgets ...   // x in [0,w], y >= contentTop()
//     ... (optional) resizable(contentPane);   // tool mode: which widget resizes
//     end();                         // Fl_Window::end()
//     finalizeShell();               // wire caption callbacks + sizing policy
//
// Tool windows: after show(), call applyToolChrome() to install native edge
// resize / corners / icon / taskbar (identical to the old setupToolChrome).
#pragma once

#include <FL/Fl_Double_Window.H>
#include "ui/Layout.h"   // TITLE_H (extern global set at startup)

class Theme;
class TitleBar;
class WindowFrame;
class Config;

class DialogBase : public Fl_Double_Window {
public:
    // Window behavior mode.
    enum Mode {
        ModalDialog = 0,   // set_modal() + fixed size + close-only caption
        ToolWindow  = 1,   // standalone, resizable, full four-button title bar
    };

    // Modal dialog: takes an externally-owned theme (from the caller).
    DialogBase(int W, int H, const char *title,
               const Theme *theme, int uiFontSize, Mode mode);

    // Tool window: takes a Config and loads/owns its own Theme internally
    // (standalone processes have no caller-provided table).
    DialogBase(Config *cfg, int W, int H, const char *title,
               int uiFontSize, Mode mode);

    ~DialogBase() override;

    // y() coordinate where content begins (just below the title bar).
    int contentTop() const { return TITLE_H; }

    // Convenience, so subclasses don't need to keep theme pointers.
    const Theme *theme() const { return m_theme; }
    int          uiFontSize() const { return m_fontSize; }

    // Override which caption buttons are shown. Tool windows pass
    // TitleBar::BTN_MASK_ALL; simple modal dialogs pass BTN_MASK_CLOSE_ONLY.
    // Per-mode default is already applied; call this to change it.
    void setCaptionButtons(int mask);

    // Tool windows call this after show() to install native edge-resize /
    // corners / icon / taskbar behavior (same as the old setupToolChrome).
    void applyToolChrome();

protected:
    // Build the shared title bar + (tool mode) WindowFrame. Call FIRST,
    // before adding any content widget.
    void initShell(const char *title);

    // Wire caption callbacks and sizing policy. Call LAST, after end().
    void finalizeShell();

    // --- Optional overrides ------------------------------------------
    // React to the caption buttons. Defaults: modal & tool both just close;
    // maximize toggles via WindowFrame; pin toggles topmost. Override to
    // extend (e.g. a tool window stopping worker threads on close).
    virtual void onCaptionClose() { hide(); }
    virtual void onCaptionMaximize();
    virtual void onCaptionTogglePin();

    TitleBar      *m_titleBar = nullptr;
    WindowFrame   *m_frame    = nullptr;
    const Theme   *m_theme    = nullptr;
    int            m_fontSize = 16;
    Mode           m_mode     = ModalDialog;

protected:
    void draw() FL_OVERRIDE;
    int  handle(int event) FL_OVERRIDE;
    // Keep the custom title bar spanning the full window width whenever the
    // window resizes. Some dialogs (e.g. ParamDialog) compute their final
    // width inside the constructor via size(); the title bar must track it,
    // otherwise the close button lands outside the window.
    void resize(int X, int Y, int W, int H) FL_OVERRIDE;

private:
    // Common constructor body (set_modal/border/bg color shared by modes).
    void commonCtor();

    bool m_pinned = false;
    // Owned theme (tool-window mode): DialogBase loads & deletes it.
    Theme *m_ownedTheme = nullptr;
};
