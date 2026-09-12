// UiBridge.cpp - UI-layer implementation of the UiBridge seam. Wires
// core/editor notifications to ConfirmDialog. Installed by MainWindow
// at startup via setUiBridge().
#include "ui/ConfirmDialog.h"
#include "core/UiBridge.h"
#include "core/Theme.h"

namespace {

class UiBridgeImpl : public UiBridge {
public:
    explicit UiBridgeImpl(const Theme *theme, int uiFontSize)
        : m_theme(theme), m_fontSize(uiFontSize) {}

    void message(const char *title, const char *msg, const char *button) override {
        ConfirmDialog(title, msg, button, nullptr, nullptr, m_theme, m_fontSize).run();
    }

    ConfirmChoice confirm(const char *title, const char *msg,
                          const char *b0, const char *b1, const char *b2) override {
        int r = ConfirmDialog(title, msg, b0, b1, b2, m_theme, m_fontSize).run();
        switch (r) {
        case 0: return ConfirmChoice::First;
        case 1: return ConfirmChoice::Second;
        case 2: return ConfirmChoice::Third;
        default: return ConfirmChoice::Dismissed;
        }
    }

private:
    const Theme *m_theme;
    int          m_fontSize;
};

} // namespace

void installUiBridge(const Theme *theme, int uiFontSize) {
    static UiBridgeImpl impl(theme, uiFontSize);
    setUiBridge(&impl);
}
