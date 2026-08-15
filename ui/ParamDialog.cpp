// ParamDialog.cpp - adaptive modal parameter-input dialog for Lua scripts
// (and the future Lua window API). Width/height are content-driven.
#include "ui/ParamDialog.h"
#include "core/I18n.h"
#include "core/Config.h"
#include "core/Theme.h"
#include "ui/ThemeWidgets.h"
#include "ui/DialogBase.h"
#include "ui/Layout.h"
#include "core/ShortcutCore.h"

#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <cstring>

static const int MARGIN = 16;
static const int GAP = 8;
static const int ROW_H = 26;      // one label+input row
static const int IN_W = 260;      // input field width
static const int kMinW = 360;     // adaptive width clamps
static const int kMaxW = 680;

// Symmetric top/bottom gaps, same config value as the Options dialog
// (settings.ini dialog_pad), so the dialog never hugs its edges.
static int dialogPad() {
    static int pad = -1;
    if (pad < 0) {
        Config cfg;
        pad = cfg.getDialogPad();
    }
    return pad;
}

ParamDialog::ParamDialog(const char *title,
                         const std::vector<LuaParam> &params,
                         const Theme *theme, int uiFontSize)
    : DialogBase(400, 0, title, theme, uiFontSize, ModalDialog), m_theme(theme) {
    begin();
    initShell(title);

    const int rows = (int)params.size();
    const int pad = dialogPad();
    const int fs = uiFontSize ? uiFontSize : 14;

    // Adaptive width: longest label + input + margins (clamped).
    fl_font(FL_HELVETICA, fs);
    int maxLbl = 0;
    for (const LuaParam &prm : params) {
        const char *lbl = prm.description.empty() ? prm.name.c_str()
                                                  : prm.description.c_str();
        maxLbl = std::max(maxLbl, (int)fl_width(lbl));
    }
    int winW = 2 * MARGIN + maxLbl + GAP + IN_W;
    winW = std::max(kMinW, std::min(kMaxW, winW));

    // Adaptive height: title bar + pad + rows + pad + button bar.
    int contentH = rows * ROW_H + (rows > 1 ? (rows - 1) * GAP : 0);
    int winH = TITLE_H + pad + contentH + pad + gBarH;
    size(winW, winH);

    // Parameter rows: label + input (default pre-filled), top-down.
    int lblW = winW - 2 * MARGIN - IN_W - GAP;
    int y = TITLE_H + pad;
    for (const LuaParam &prm : params) {
        std::string label = prm.description.empty() ? prm.name : prm.description;

        Fl_Box *lbl = nullptr;
        if (!prm.isCheckbox) {
            // Checkbox rows carry their own label (find-bar Match Case
            // style) - the left label box would duplicate the text.
            lbl = new Fl_Box(MARGIN, y, lblW, ROW_H, nullptr);
            // Fl_Widget only stores the label POINTER - copy_label() so
            // the label survives the loop-local std::string (dangling
            // pointer showed garbage once the heap layout shifted).
            lbl->copy_label(label.c_str());
            lbl->box(FL_NO_BOX);
            lbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
            lbl->labelsize(fs);
            lbl->labelcolor(theme ? theme->colors().textPrimary : FL_BLACK);
        }

        if (prm.isCheckbox) {
            // Boolean switch: checkbox identical to the find bar's
            // "Match Case" (FL_NO_BOX + FL_BORDER_BOX down box + theme
            // colors), label to the right of the box. Checked state is
            // independent of declaration order: checked = the on/true
            // option, unchecked = the off/false option.
            std::string onV, offV;
            for (const auto &c : prm.choices) {
                if (c == "on" || c == "true") { if (onV.empty()) onV = c; }
                if (c == "off" || c == "false") { if (offV.empty()) offV = c; }
            }
            Fl_Check_Button *cb = new Fl_Check_Button(MARGIN + lblW + GAP, y,
                                                      IN_W, ROW_H, nullptr);
            // Fl_Widget stores the label POINTER only - copy_label() so
            // the text survives the loop-local std::string (same dangling
            // pointer bug the Fl_Box rows had).
            cb->copy_label(label.c_str());
            cb->box(FL_NO_BOX);
            cb->down_box(FL_BORDER_BOX);
            cb->color(theme ? theme->colors().bgEditor : FL_WHITE);
            cb->selection_color(theme ? theme->colors().accentSelection : FL_SELECTION_COLOR);
            cb->labelsize(fs);
            cb->labelcolor(theme ? theme->colors().textPrimary : FL_BLACK);
            // Default checked only when the declared default IS the on/true
            // option (so `case=off|on` renders unchecked by default).
            cb->value(!onV.empty() && prm.defValue == onV ? 1 : 0);
            m_checks.push_back(cb);
            m_params.push_back(prm);
        } else if (!prm.choices.empty()) {
            // Dropdown: choices from the --!param declaration, pre-selected
            // to the default (first option). Same style as the Options
            // dialog's Tab width / Auto save choices.
            SettingsChoice *ch = new SettingsChoice(MARGIN + lblW + GAP, y, IN_W, ROW_H, nullptr);
            ch->box(FL_BORDER_BOX);
            ch->down_box(FL_BORDER_BOX);
            ch->textsize(fs);
            ch->color(theme ? theme->colors().bgEditor : FL_WHITE);
            ch->selection_color(theme ? theme->colors().accentSelection : FL_SELECTION_COLOR);
            ch->textcolor(theme ? theme->colors().textPrimary : FL_BLACK);
            int defIdx = 0;
            for (size_t k = 0; k < prm.choices.size(); ++k) {
                const std::string &choiceLabel =
                    (k < prm.choiceLabels.size() && !prm.choiceLabels[k].empty())
                        ? prm.choiceLabels[k] : prm.choices[k];
                ch->add(choiceLabel.c_str());
                if (prm.choices[k] == prm.defValue) defIdx = (int)k;
            }
            ch->value(defIdx);
            ch->callback(cbKey, this);   // Enter acts like OK
            m_choices.push_back(ch);
            m_params.push_back(prm);
        } else {
            // Unified themed input: theme background/text/caret/selection
            // (same look as the main editor and every other Pecia input).
            ThemeColors tc;
            if (m_theme) tc = m_theme->colors();
            else         tc.applyPreset(THEME_PRESET_LIGHT);
            ThemedInput *in = new ThemedInput(MARGIN + lblW + GAP, y, IN_W, ROW_H, tc, nullptr);
            in->textsize(fs);
            in->value(prm.defValue.c_str());
            in->when(FL_WHEN_ENTER_KEY);   // Enter in an input -> callback
            in->callback(cbKey, this);
            m_inputs.push_back(in);
            m_params.push_back(prm);
        }

        y += ROW_H + GAP;
    }

    // Bottom button bar (chrome, gBarH tall): OK only; the title bar X
    // doubles as cancel.
    Fl_Color chromeCol = theme ? theme->colors().bgChrome : FL_BACKGROUND2_COLOR;
    Fl_Color btnFg = theme ? theme->colors().textPrimary : FL_BLACK;
    int btnBarY = TITLE_H + pad + contentH + pad;
    Fl_Group *btnBar = new Fl_Group(0, btnBarY, winW, gBarH);
    btnBar->box(FL_FLAT_BOX);
    btnBar->color(chromeCol);

    int btnY = btnBarY + (gBarH - gBtnH) / 2;
    m_ok = new HoverButton(0, btnY, 0, gBtnH, I18n::get("settings.ok"));
    m_ok->color(chromeCol);
    m_ok->selection_color(theme ? theme->colors().accentSelection : FL_SELECTION_COLOR);
    m_ok->labelcolor(btnFg);
    m_ok->labelsize(uiFontSize);
    m_ok->callback(cbBtn, this);

    // Auto-width right-aligned single OK button (the title bar X is cancel).
    fitButtonRow({m_ok}, winW, btnBarY + gBarH / 2, MARGIN, GAP);

    btnBar->end();
    end();
    finalizeShell();

    // Focus the first input, select its default text for easy overwrite.
    if (!m_inputs.empty()) {
        m_inputs[0]->take_focus();
        m_inputs[0]->insert_position(0, m_inputs[0]->size());   // 1.4.0 API
    } else if (!m_choices.empty()) {
        m_choices[0]->take_focus();
    } else if (!m_checks.empty()) {
        m_checks[0]->take_focus();
    }
}

ParamDialog::~ParamDialog() = default;

int ParamDialog::handle(int event) {
    // OK shortcut (Window > Button > OK). Checked on FL_KEYDOWN so it
    // wins over the input fields' Enter handling.
    if (event == FL_KEYDOWN) {
        if (dialogOkShortcutMatches(Fl::event_key(), Fl::event_state())) {
            cbBtn(m_ok, this);
            return 1;
        }
    }
    return DialogBase::handle(event);
}

bool ParamDialog::run() {
    // Center on screen.
    position((Fl::w() - w()) / 2, (Fl::h() - h()) / 2);
    m_result = 0;
    show();
    while (shown()) Fl::wait();
    return m_result != 0;
}

void ParamDialog::collectAndOk(ParamDialog *self) {
    self->m_values.clear();
    size_t ci = 0, ii = 0, ki = 0;
    for (const LuaParam &prm : self->m_params) {
        if (prm.isCheckbox) {
            // Checked = the on/true option, unchecked = off/false
            // (order-independent, so scripts always see on/off or
            // true/false semantics).
            int want = self->m_checks[ki]->value() ? 0 : 1;  // 0 = prefer on/true
            int sel = -1;
            for (size_t k = 0; k < prm.choices.size(); ++k) {
                bool isOn = (prm.choices[k] == "on" || prm.choices[k] == "true");
                if (want == 0 && isOn) { sel = (int)k; break; }
                if (want == 1 && !isOn) { sel = (int)k; break; }
            }
            if (sel >= 0)
                self->m_values.push_back(prm.choices[(size_t)sel]);
            else
                self->m_values.push_back("");
            ++ki;
        } else if (!prm.choices.empty()) {
            // Return the RAW option value (choices[k]), not the
            // translated display label - scripts match on values.
            int sel = self->m_choices[ci]->value();
            if (sel >= 0 && sel < (int)prm.choices.size())
                self->m_values.push_back(prm.choices[(size_t)sel]);
            else
                self->m_values.push_back("");
            ++ci;
        } else {
            const char *v = self->m_inputs[ii]->value();
            self->m_values.push_back(v ? v : "");
            ++ii;
        }
    }
    self->m_result = 1;
    self->hide();
}

void ParamDialog::cbBtn(Fl_Widget *w, void *data) {
    auto *self = static_cast<ParamDialog *>(data);
    if (w == self->m_ok) {
        collectAndOk(self);
    } else {
        self->m_result = 0;   // cancel (X / other)
        self->hide();
    }
}

void ParamDialog::cbKey(Fl_Widget *w, void *data) {
    // Enter in an input field acts like OK. Guard: m_inputs may be empty
    // (pure checkbox/choice dialog) - nothing to submit via Enter, and
    // .back() on an empty vector is undefined behavior.
    auto *self = static_cast<ParamDialog *>(data);
    if (self->m_inputs.empty()) return;
    if (w == self->m_inputs.back()) collectAndOk(self);
}

