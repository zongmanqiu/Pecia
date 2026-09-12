// UiBridge.h - dependency-inversion seam between the core/editor layers
// and the UI layer. Core code (Document, FileManager) must not include
// FLTK widgets or dialog classes; instead they report user-facing events
// through this interface and MainWindow injects a concrete implementation.
#pragma once

#include <functional>

// Result of a three-way confirmation dialog (matches the buttons
// "Save" / "Don't Save" / "Cancel" used for unsaved-changes prompts).
enum class ConfirmChoice {
    First,    // first button (e.g. "Save")
    Second,   // second button (e.g. "Don't Save")
    Third,    // third button (e.g. "Cancel")
    Dismissed // window closed without choosing
};

// UiBridge: the one way core/editor code talks to the user. The
// implementation lives in the UI layer (see ui/UiBridge.cpp / MainWindow).
class UiBridge {
public:
    virtual ~UiBridge() = default;

    // Show a single-button message dialog (title, message, button text).
    // Returns when the user dismisses it.
    virtual void message(const char *title, const char *msg, const char *button) = 0;

    // Show a three-button confirmation dialog. Returns the chosen button.
    virtual ConfirmChoice confirm(const char *title, const char *msg,
                                  const char *b0, const char *b1, const char *b2) = 0;
};

// Global bridge accessor. MainWindow installs the UI-backed implementation
// at startup; core code may call this without knowing what UI is in use.
// A null bridge is safe to call: every method becomes a no-op / default.
UiBridge *uiBridge();
void setUiBridge(UiBridge *bridge);

// UI-layer helper: install the ConfirmDialog-backed bridge (ui/UiBridge.cpp).
// Call once at application startup before any core code may show dialogs.
class Theme;
void installUiBridge(const Theme *theme, int uiFontSize);
