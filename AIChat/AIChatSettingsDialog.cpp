// AIChatSettingsDialog.cpp - modal AI configuration dialog.
// Mirrors the SettingsDialog look & feel: info title bar on top, flat
// content area, bottom chrome button bar with right-aligned auto-width
// buttons, and a 1px outer border drawn on top of everything.
#include "AIChatSettingsDialog.h"

#include "core/Config.h"
#include "core/I18n.h"
#include "core/Theme.h"
#include "ui/ThemeWidgets.h"   // HoverButton
#include "ui/Layout.h"
#include "core/ShortcutCore.h"

#include <FL/Fl_Box.H>
#include <FL/Fl_Input.H>
#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <string>
#include <windows.h>   // ShellExecuteA (open GLM page)

namespace {
const int MARGIN = 12;
const int GAP = 8;
const int ROW_H = 24;
const int LBL_W = 110;
const int IN_W = 320;
const int BTN_H = gBtnH;   // unified button height
const int WIN_W = MARGIN * 2 + LBL_W + IN_W;
} // namespace

AIChatSettingsDialog::AIChatSettingsDialog(Config *appCfg, const Theme *theme,
                                           int uiFontSize)
    : DialogBase(WIN_W, 0, I18n::get("ai.section"), theme, uiFontSize, ModalDialog)
    , m_cfg(appCfg) {
    begin();
    initShell(I18n::get("ai.section"));

    // Content area: three input rows, padded from the title bar and the
    // button bar so fields don't touch them. Padding is the single config
    // value dialog_pad (settings.ini) shared by all dialogs.
    int pad = m_cfg ? m_cfg->getDialogPad() : 16;
    int contentH = 3 * ROW_H + 2 * GAP;
    int winH = TITLE_H + pad + contentH + pad + gBarH;
    size(WIN_W, winH);

    int scrollY = TITLE_H;
    int y = scrollY + pad;

    Fl_Color fg = theme ? theme->colors().textPrimary : FL_BLACK;
    auto mkLabel = [&](int yy, const char *text) {
        Fl_Box *lbl = new Fl_Box(FL_NO_BOX, MARGIN, yy, LBL_W, ROW_H, text);
        lbl->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        lbl->labelsize(uiFontSize);
        lbl->labelcolor(fg);
    };
    auto mkInput = [&](int yy) -> Fl_Input * {
        Fl_Input *in = new Fl_Input(MARGIN + LBL_W, yy, IN_W, ROW_H);
        in->box(FL_BORDER_BOX);   // rectangular flat border, like SettingsDialog
        in->textsize(uiFontSize);
        in->textcolor(fg);
        in->selection_color(theme ? theme->colors().accentSelection
                                  : FL_SELECTION_COLOR);
        return in;
    };

    mkLabel(y, I18n::get("ai.endpoint"));
    m_endpoint = mkInput(y);
    y += ROW_H + GAP;
    mkLabel(y, I18n::get("ai.model"));
    m_model = mkInput(y);
    y += ROW_H + GAP;
    mkLabel(y, I18n::get("ai.key"));
    m_key = mkInput(y);
    m_key->tooltip(I18n::get("ai.keyhint"));
    y += ROW_H;

    // --- Bottom button bar: same height as title bar (SettingsDialog style) ---
    int btnBarY = y + pad;   // flush after the content area
    Fl_Color chromeCol = theme ? theme->colors().bgChrome : FL_BACKGROUND2_COLOR;
    Fl_Group *btnBar = new Fl_Group(0, btnBarY, WIN_W, gBarH);
    btnBar->box(FL_FLAT_BOX);
    btnBar->color(chromeCol);

    int btnX, btnY = btnBarY + (gBarH - BTN_H) / 2;   // vertically centered
    HoverButton *glmBtn = new HoverButton(0, btnY, 0, BTN_H, I18n::get("ai.glmapi"));
    HoverButton *ok = new HoverButton(0, btnY, 0, BTN_H, I18n::get("settings.ok"));
    glmBtn->color(chromeCol);
    glmBtn->selection_color(theme ? theme->colors().accentSelection
                                  : FL_SELECTION_COLOR);
    glmBtn->labelsize(uiFontSize);
    glmBtn->labelcolor(fg);
    glmBtn->callback(cbGLM, this);
    ok->color(chromeCol);
    ok->selection_color(theme ? theme->colors().accentSelection
                              : FL_SELECTION_COLOR);
    ok->labelsize(uiFontSize);
    ok->labelcolor(fg);
    ok->callback(cbOk, this);

    // Width adapts to the label text; buttons stay right-aligned.
    // Layout: [GLM api] [OK]  (no Cancel - the title bar close button covers it)
    int okW = 0, okH = 0, glmW = 0, glmH = 0;
    ok->measure_label(okW, okH);
    glmBtn->measure_label(glmW, glmH);
    okW += 24; if (okW < 40) okW = 40;
    glmW += 24; if (glmW < 40) glmW = 40;
    int totalBtnW = glmW + okW + GAP;
    btnX = WIN_W - MARGIN - totalBtnW;
    glmBtn->resize(btnX, btnY, glmW, BTN_H);
    ok->resize(btnX + glmW + GAP, btnY, okW, BTN_H);
    btnBar->resizable(nullptr);
    btnBar->end();

    // The shared title bar is created by DialogBase.
    end();
    finalizeShell();
}

bool AIChatSettingsDialog::runModal() {
    if (!m_cfg) return false;
    char buf[512];
    m_cfg->getAiEndpoint(buf, sizeof(buf),
        "https://open.bigmodel.cn/api/paas/v4/chat/completions");
    m_endpoint->value(buf);
    m_cfg->getAiModel(buf, sizeof(buf), "glm-4-flash");
    m_model->value(buf);
    m_cfg->getAiKey(buf, sizeof(buf), "");
    m_key->value(buf);

    m_ok = false;
    position((Fl::w() - w()) / 2, (Fl::h() - h()) / 2);
    show();
    while (shown()) Fl::wait();
    return m_ok;
}

int AIChatSettingsDialog::handle(int event) {
    // OK shortcut (Window > Button > OK): triggers the OK button.
    if (event == FL_KEYDOWN || event == FL_SHORTCUT) {
        if (dialogOkShortcutMatches(Fl::event_key(), Fl::event_state())) {
            cbOk(nullptr, this);
            return 1;
        }
    }
    return DialogBase::handle(event);
}

void AIChatSettingsDialog::cbOk(Fl_Widget *, void *data) {
    AIChatSettingsDialog *self = static_cast<AIChatSettingsDialog *>(data);
    if (!self || !self->m_cfg) return;
    // Trim whitespace from hand-entered values before persisting.
    auto trim = [](const char *s) -> std::string {
        std::string v = s ? s : "";
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
        size_t i = 0;
        while (i < v.size() && (v[i] == ' ' || v[i] == '\t')) ++i;
        return v.substr(i);
    };
    self->m_cfg->setAiEndpoint(trim(self->m_endpoint->value()).c_str());
    self->m_cfg->setAiModel(trim(self->m_model->value()).c_str());
    self->m_cfg->setAiKey(trim(self->m_key->value()).c_str());
    self->m_ok = true;
    self->hide();
}

void AIChatSettingsDialog::cbGLM(Fl_Widget *, void * /*data*/) {
    // Open the Zhipu GLM coding-plan page so the user can apply for an
    // API key. Title-bar close button replaces the old Cancel button.
    ShellExecuteA(nullptr, "open",
                  "https://bigmodel.cn/coding-plan/personal/overview",
                  nullptr, nullptr, SW_SHOWNORMAL);
}

