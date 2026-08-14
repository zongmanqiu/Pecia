// ShortcutDialog.cpp - modal dialog for customizing keyboard shortcuts.
#include "ui/ShortcutDialog.h"

#include "core/Config.h"
#include "core/I18n.h"
#include "core/Theme.h"
#include "ui/Layout.h"
#include "ui/ThemeWidgets.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Scroll.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <string>
#include <vector>

// Clip `s` to fit `maxW` pixels at the current fl_font: shorten the
// text and append "..." (binary search on the byte length, then back
// off to a UTF-8 character boundary so multi-byte characters are never
// split). The full name is always available via the row tooltip.
static std::string clipEllipsis(const std::string &s, int maxW) {
    if (maxW < 24) return s.substr(0, 1);
    int tw = 0, th = 0;
    fl_measure(s.c_str(), tw, th);
    if (tw <= maxW) return s;
    const std::string ell = "...";
    int lo = 0, hi = (int)s.size();
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        std::string t = s.substr(0, mid) + ell;
        fl_measure(t.c_str(), tw, th);
        if (tw <= maxW) lo = mid; else hi = mid - 1;
    }
    // Back off to the start of the last UTF-8 character.
    while (lo > 0 && ((unsigned char)s[lo] & 0xC0) == 0x80) --lo;
    return s.substr(0, lo) + ell;
}

// ---------------------------------------------------------------------------
// RowWidget: one action row (name | combo box). Clicking the combo box
// clears the current shortcut and starts recording; pressing a key then
// captures it. Clicking anywhere else (or losing focus) cancels the
// recording and leaves the combo empty.
// ---------------------------------------------------------------------------

class ShortcutDialog::RowWidget : public Fl_Box {
public:
    RowWidget(int X, int Y, int W, int H)
        : Fl_Box(X, Y, W, H) {
        box(FL_NO_BOX);
        // Without this, FLTK's take_focus() bails out (it requires
        // FL_VISIBLE_FOCUS and a handle(FL_FOCUS) that returns 1), the
        // row never receives keyboard focus, and recording a combo
        // after clicking the box would not work.
        set_visible_focus();
    }

    void setDialog(ShortcutDialog *dlg) { m_dlg = dlg; }
    void setIndex(int i) { m_index = i; }
    void setTheme(const Theme *t) { m_theme = t; }
    void setFontSize(int s) { m_fontSize = s; }
    void setRecording(bool on) { m_recording = on; redraw(); }
    bool isRecording() const { return m_recording; }
    void setConflict(bool on) { m_conflict = on; redraw(); }
    void setComboText(const std::string &t) { m_comboText = t; redraw(); }
    void setRowName(const std::string &t) {
        m_rowName = t;
        // Tooltip carries the FULL name - the row itself may clip it
        // with an ellipsis when the dialog width is tight.
        tooltip(t.c_str());
    }
    int index() const { return m_index; }

    int handle(int event) FL_OVERRIDE {
        if (event == FL_PUSH) {
            bool inCombo = (Fl::event_x() >= comboX()) &&
                           (Fl::event_x() < comboX() + kComboW);
            if (m_dlg) {
                if (inCombo) m_dlg->onRowComboClicked(this);
                else         m_dlg->onRowBlur(this);   // click elsewhere cancels
            }
            return 1;
        }
        if (event == FL_KEYDOWN && m_recording) {
            int key = Fl::event_key();
            // Ignore modifier keys themselves; wait for the real key.
            switch (key) {
            case FL_Control_L: case FL_Control_R:
            case FL_Shift_L: case FL_Shift_R:
            case FL_Alt_L: case FL_Alt_R:
            case FL_Meta_L: case FL_Meta_R:
            case FL_Caps_Lock: case FL_Num_Lock: case FL_Scroll_Lock:
                return 1;
            default:
                break;
            }
            ShortcutCombo combo;
            combo.key = key;
            combo.mods = Fl::event_state() & (FL_CTRL | FL_SHIFT | FL_ALT);
            if (m_dlg) m_dlg->onRowComboChanged(this, combo);
            return 1;
        }
        if (event == FL_UNFOCUS && m_recording) {
            if (m_dlg) m_dlg->onRowBlur(this);
            return 1;
        }
        // Accept focus (and keep it) so the row can receive FL_KEYDOWN
        // while recording.
        if (event == FL_FOCUS) return 1;
        return Fl_Box::handle(event);
    }

    void draw() FL_OVERRIDE {
        Fl_Box::draw();
        if (!m_theme) return;
        const ThemeColors &c = m_theme->colors();

        // Row name (left), clipped to leave room for the combo box.
        // Color stays the primary text color even while recording.
        fl_color(c.textPrimary);
        fl_font(FL_HELVETICA, m_fontSize);
        int avail = w() - kComboW - 32;
        std::string clipped = clipEllipsis(m_rowName, avail - 12);
        fl_draw(clipped.c_str(), x() + 12, y(), avail, h(),
                FL_ALIGN_LEFT | FL_ALIGN_CENTER, nullptr, 0);

        // Combo box (right): bordered, filled with the panel color;
        // recording gets the accent background. Height matches the
        // dialog's buttons (24 px).
        int cx = comboX();
        int cy = y() + 1;
        int ch = h() - 2;
        fl_color(m_recording ? c.accentSelection : c.bgPanel);
        fl_rectf(cx, cy, kComboW, ch);
        fl_color(m_conflict ? fl_rgb_color(200, 30, 30) : c.borderColor);
        fl_rect(cx, cy, kComboW, ch);
        fl_color(m_recording ? c.textPrimary
                             : (m_conflict ? fl_rgb_color(200, 30, 30)
                                           : c.textPrimary));
        fl_font(FL_HELVETICA, m_fontSize);
        // Left-aligned so all shortcut texts share one column ("F3",
        // "Ctrl+F" and "Delete" start at the same x); centering would
        // make each row's text float at a different offset.
        fl_draw(m_comboText.c_str(), cx + 8, cy, kComboW - 12, ch,
                FL_ALIGN_LEFT | FL_ALIGN_CENTER, nullptr, 0);
    }

    // take_focus is not virtual in FLTK's Fl_Widget; the plain overload
    // just sets the focus so the row receives FL_KEYDOWN while recording.
    int take_focus() { return Fl_Box::take_focus(); }

private:
    static const int kComboW = 130;
    int comboX() const { return x() + w() - kComboW - 12; }

    ShortcutDialog *m_dlg = nullptr;
    const Theme *m_theme = nullptr;
    int m_index = -1;
    int m_fontSize = 16;
    bool m_recording = false;
    bool m_conflict = false;
    std::string m_rowName;
    std::string m_comboText;
};

// Scroll group with the main window's custom scrollbar look (10px flat
// thumb/track, no arrows) and no border lines.
class ShortcutDialog::SettingsScroll : public Fl_Scroll {
public:
    SettingsScroll(int X, int Y, int W, int H, const Theme *theme)
        : Fl_Scroll(X, Y, W, H), m_theme(theme) {
        scrollbar_size(10);
        scrollbar.type(FL_VERT_SLIDER);
        if (theme) styleToolScrollbar(&scrollbar, FL_VERT_SLIDER,
                                      theme->colors());
    }
    void draw() FL_OVERRIDE {
        Fl_Scroll::draw();
        if (m_theme) drawToolScrollbar(&scrollbar, false, m_theme->colors());
    }

private:
    const Theme *m_theme;
};

// ---------------------------------------------------------------------------

static const int kFieldH = 22;              // find/go-to bar input height (FIELD_H)
static const int kIconW = 24;               // search-icon inset inside the input

// Input boxtype identical to FL_BORDER_BOX but with a 24 px left inset,
// so the input text starts AFTER the search-magnifier icon.
static Fl_Boxtype s_searchBoxtype = FL_NO_BOX;

static void ensureSearchBoxtype() {
    if (s_searchBoxtype != FL_NO_BOX) return;
    s_searchBoxtype = (Fl_Boxtype)FL_FREE_BOXTYPE;
    Fl::set_boxtype(s_searchBoxtype, Fl::get_boxtype(FL_BORDER_BOX),
                    24, 1, 26, 2, nullptr);
}

// Magnifier icon drawn inside the filter input's left inset.
class SearchIconBox : public Fl_Box {
public:
    SearchIconBox(int X, int Y, int W, int H, Fl_Color c)
        : Fl_Box(X, Y, W, H), m_color(c) {
        box(FL_NO_BOX);
    }
    void draw() FL_OVERRIDE {
        Fl_Box::draw();
        fl_color(m_color);
        fl_line_style(FL_SOLID, 2);
        fl_arc(x() + 1, y() + 1, 11, 11, 0, 360);          // lens
        fl_line(x() + 10, y() + 10, x() + 15, y() + 15);   // handle
        fl_line_style(FL_SOLID, 1);
    }

private:
    Fl_Color m_color;
};

ShortcutDialog::ShortcutDialog(int w, int h, const char *title, Config *cfg,
                               const Theme *theme, int uiFontSize)
    : DialogBase(w, h + TITLE_H, title, theme, uiFontSize, ModalDialog),
      m_cfg(cfg), m_theme(theme), m_uiFontSize(uiFontSize),
      m_pad(cfg ? cfg->getDialogPad() : 16) {
    begin();
    initShell(title);

    int margin = 12;
    int btnH = gBtnH;
    // Bottom button bar (bgChrome, gBarH tall, flush against the filter
    // bar). The filter bar and button bar live at the bottom of the
    // content area (0..h); the window adds the title bar on top.
    int btnBarY = TITLE_H + h - gBarH;   // bottom bar top (window coords)
    // Compact layout, no dead space (top -> bottom): title bar, scroll
    // (flush against the title bar and the filter bar), filter bar
    // (find-bar look, flush against the button bar), button bar. The
    // filter bar is FINDBAR_H tall with the input vertically centered
    // (small symmetric gaps above/below, like the find bar's field).
    int filterTop = btnBarY - GOTOBAR_H;   // find-bar look, flush to buttons
    int scrollY = TITLE_H;
    int scrollH = filterTop - scrollY;

    SettingsScroll *scroll = new SettingsScroll(0, scrollY, w, scrollH, theme);
    scroll->type(Fl_Scroll::VERTICAL);
    scroll->box(FL_FLAT_BOX);
    scroll->color(theme ? theme->colors().bgEditor : FL_WHITE);
    scroll->end();

    // Filter bar at the bottom, styled exactly like the find bar:
    // bgPanel background, one FL_BORDER_BOX input with the accent
    // selection color and a light in-input hint. A magnifier icon sits
    // in the input's left inset (the custom boxtype shifts the text
    // past it). Recording / error feedback shares the in-input hint
    // area, so the bar never grows a second row.
    ensureSearchBoxtype();
    Fl_Group *filterBar = new Fl_Group(0, filterTop, w, GOTOBAR_H);
    filterBar->box(FL_FLAT_BOX);
    filterBar->color(theme ? theme->colors().bgPanel : fl_rgb_color(245, 245, 245));

    m_filterInput = new Fl_Input(24,
                                 filterTop + (GOTOBAR_H - kFieldH) / 2,
                                 w - 48, kFieldH);
    m_filterInput->textsize(uiFontSize);
    m_filterInput->box(s_searchBoxtype);
    m_filterInput->color(theme ? theme->colors().bgEditor : FL_WHITE);
    m_filterInput->textcolor(theme ? theme->colors().textPrimary : FL_BLACK);
    m_filterInput->selection_color(theme ? theme->colors().accentSelection
                                         : FL_SELECTION_COLOR);
    m_filterInput->callback(cbFilter, this);
    m_filterInput->when(FL_WHEN_CHANGED | FL_WHEN_RELEASE);

    // Light in-input hint (same look as the find bar's "Find:" note),
    // placed after the icon inset. Doubles as the recording prompt and
    // error line (setHintText()).
    m_hintLabel = new Fl_Box(FL_NO_BOX, m_filterInput->x() + kIconW + 4,
                             m_filterInput->y(),
                             w - 48 - kIconW - 8, kFieldH,
                             I18n::get("shortcut.dialog.filter"));
    m_hintLabel->labelsize(uiFontSize);
    m_hintLabel->labelcolor(theme ? theme->colors().textSecondary
                                  : fl_rgb_color(140, 140, 140));
    m_hintLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    // Magnifier icon inside the input's left inset.
    new SearchIconBox(m_filterInput->x() + 5,
                      m_filterInput->y() + (kFieldH - 16) / 2 + 1,
                      16, 16,
                      theme ? theme->colors().textSecondary
                            : fl_rgb_color(140, 140, 140));

    filterBar->resizable(nullptr);
    filterBar->end();
    updateHintVisibility();

    // Bottom button bar.
    Fl_Group *btnBar = new Fl_Group(0, btnBarY, w, gBarH);
    btnBar->box(FL_FLAT_BOX);
    btnBar->color(theme ? theme->colors().bgChrome : FL_BACKGROUND2_COLOR);

    Fl_Color chromeCol = theme ? theme->colors().bgChrome : FL_BACKGROUND2_COLOR;
    Fl_Color fg = theme ? theme->colors().textPrimary : FL_BLACK;
    Fl_Color sel = theme ? theme->colors().accentSelection : FL_SELECTION_COLOR;

    int btnY = btnBarY + (gBarH - btnH) / 2;
    const int gap = 8;
    // Right-aligned: [Restore Defaults] [OK] [Cancel].
    m_resetBtn = new HoverButton(0, btnY, 0, btnH,
                                 I18n::get("shortcut.dialog.reset"));
    m_okBtn = new HoverButton(0, btnY, 0, btnH, I18n::get("settings.ok"));
    m_cancelBtn = new HoverButton(0, btnY, 0, btnH,
                                  I18n::get("settings.cancel"));
    for (auto *b : { m_resetBtn, m_okBtn, m_cancelBtn }) {
        b->color(chromeCol);
        b->selection_color(sel);
        b->labelsize(uiFontSize);
        b->labelcolor(fg);
    }
    m_okBtn->callback(cbOk, this);
    m_cancelBtn->callback(cbCancel, this);
    m_resetBtn->callback(cbReset, this);

    // Auto-width right-aligned row: [Restore Defaults] [OK] [Cancel].
    fitButtonRow({m_resetBtn, m_okBtn, m_cancelBtn}, w,
                 btnBarY + gBarH / 2, margin, gap);

    btnBar->resizable(nullptr);
    btnBar->end();

    end();
    finalizeShell();
}

ShortcutDialog::~ShortcutDialog() = default;

void ShortcutDialog::setRows(const std::vector<ShortcutDialogRow> &rows) {
    m_rows.clear();
    m_rows.reserve(rows.size());
    for (const auto &r : rows) {
        RowState st;
        st.def = r;
        // Effective combo: an explicit config value wins; an EMPTY
        // value is a real "no shortcut" (cleared by the user), only a
        // MISSING key falls back to the built-in default.
        char key[160];
        snprintf(key, sizeof(key), "shortcut.%s", r.id.c_str());
        std::string text = m_cfg ? m_cfg->getShortcut(r.id.c_str()) : "";
        ShortcutCombo combo;
        if (m_cfg && m_cfg->hasKey(key)) {
            if (!text.empty()) shortcutParse(text, &combo);
        } else {
            combo.key = r.defKey;
            combo.mods = r.defMods;
        }
        st.combo = combo;
        m_rows.push_back(st);
    }
    layoutRows();
}

// Build the row widgets from m_rows (called after setRows()): one
// widget per row (index-stable), then position them respecting the
// active filter.
void ShortcutDialog::layoutRows() {
    for (auto *wdg : m_widgets) {
        if (wdg && wdg->parent()) wdg->parent()->remove(wdg);
        delete wdg;
    }
    m_widgets.clear();
    if (m_topSpacer && m_topSpacer->parent()) {
        m_topSpacer->parent()->remove(m_topSpacer);
        delete m_topSpacer;
        m_topSpacer = nullptr;
    }
    m_recording = nullptr;

    SettingsScroll *scroll = static_cast<SettingsScroll *>(child(0));
    if (!scroll) return;
    scroll->begin();

    // Top spacer: a visible 16 px high empty box at the very top of the
    // scroll content. Without it, FL_Scroll computes a negative initial
    // scroll position (child.t=48 vs inner top=32); dragging the
    // scrollbar then clamps to 0 and the top pad vanishes. With the
    // spacer, the content bbox starts at the scroll top and the pad is
    // part of the scrollable content, so it survives scrolling.
    m_topSpacer = new Fl_Box(FL_NO_BOX, 0, scroll->y(), 0, m_pad, nullptr);

    int margin = 12;
    int rowH = 26;

    int idx = 0;
    for (auto &st : m_rows) {
        RowWidget *rw = new RowWidget(margin, 0, w() - margin * 2, rowH);
        rw->setDialog(this);
        rw->setIndex(idx);
        rw->setTheme(m_theme);
        rw->setFontSize(m_uiFontSize);
        rw->setRowName(st.def.name);
        rw->setComboText(comboText(st.combo));
        m_widgets.push_back(rw);
        ++idx;
    }

    // Bottom gap = dialog_pad (same as the Options dialog), so the
    // rows stay the same distance from the filter bar as from the
    // title bar even when the list is short. repositionRows() keeps
    // this spacer right after the last row.
    m_bottomSpacer = new Fl_Box(FL_NO_BOX, 0, 0, 0, m_pad, nullptr);
    scroll->end();
    repositionRows();
    recomputeConflicts();
}

// Position the rows: visible rows (matching the filter) stack from the
// top with the normal row gap and a fixed dialog_pad above. The bottom
// spacer sits right after the last row so scrolling always leaves a
// dialog_pad gap at the bottom (like the Options dialog).
void ShortcutDialog::repositionRows() {
    SettingsScroll *scroll = static_cast<SettingsScroll *>(child(0));
    if (!scroll) return;
    // Reset the top spacer to the scroll top: scrolling moves every
    // child (Fl_Scroll::scroll_to repositions them), so after a scroll
    // the spacer would sit at a negative y and drag the content bbox
    // with it, breaking the layout. Reposition it on every relayout.
    if (m_topSpacer)
        m_topSpacer->resize(0, scroll->y(), 0, m_pad);

    int margin = 12;
    int rowH = 26;
    int gap = 2;

    int y = scroll->y() + m_pad;
    int lastRowBottom = y;
    for (size_t i = 0; i < m_rows.size() && i < m_widgets.size(); ++i) {
        RowWidget *rw = m_widgets[i];
        if (!rowVisible(i)) {
            rw->hide();
            continue;
        }
        rw->show();
        rw->resize(margin, y, w() - margin * 2, rowH);
        y += rowH + gap;
        lastRowBottom = y - gap;
    }

    // Bottom spacer after the last row: scrolling leaves a dialog_pad
    // gap below the rows (like the Options dialog).
    if (m_bottomSpacer)
        m_bottomSpacer->resize(0, lastRowBottom, 0, m_pad);
    scroll->redraw();
}

// Filter match: the query (lowercase) is a substring of the action's
// display name or its current shortcut text.
bool ShortcutDialog::rowVisible(size_t i) const {
    if (m_filterText.empty()) return true;
    if (i >= m_rows.size()) return false;
    const RowState &st = m_rows[i];
    auto lower = [](std::string s) {
        for (auto &c : s) {
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        }
        return s;
    };
    if (lower(st.def.name).find(m_filterText) != std::string::npos) return true;
    std::string ct = lower(shortcutToString(st.combo));
    return ct.find(m_filterText) != std::string::npos;
}

// Filter box callback: narrow the visible rows to the typed query.
void ShortcutDialog::applyFilter() {
    stopRecording();
    std::string q = m_filterInput ? m_filterInput->value() : "";
    m_filterText.clear();
    for (char c : q) {
        m_filterText += (char)((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    }
    repositionRows();
}

// Show/hide the in-input hint (same rule as the find bar): visible only
// while the input is empty and not focused.
void ShortcutDialog::updateHintVisibility() {
    if (!m_hintLabel || !m_filterInput) return;
    const char *v = m_filterInput->value();
    if (!v || !*v)
        (Fl::focus() == m_filterInput) ? m_hintLabel->hide() : m_hintLabel->show();
    else
        m_hintLabel->hide();
}

int ShortcutDialog::handle(int event) {
    // Clicking anywhere no row/widget consumed (empty area of the dialog)
    // also cancels the recording - not just clicks on other widgets
    // (those shift focus, which ends recording via FL_UNFOCUS).
    if (event == FL_PUSH && m_recording) {
        stopRecording();
        updateHintText();
    }
    // User-assignable OK shortcut (Window > Button > OK): trigger OK
    // unless a row is recording a combo (keys go to the recording row
    // then).
    if ((event == FL_KEYDOWN || event == FL_SHORTCUT) && !m_recording) {
        if (dialogOkShortcutMatches(Fl::event_key(), Fl::event_state())) {
            finishOk();
            return 1;
        }
    }
    int ret = DialogBase::handle(event);
    if (event == FL_FOCUS || event == FL_UNFOCUS || event == FL_KEYUP) {
        updateHintVisibility();
    }
    return ret;
}

void ShortcutDialog::cbFilter(Fl_Widget * /*w*/, void *data) {
    auto *self = static_cast<ShortcutDialog *>(data);
    if (self) self->applyFilter();
}

std::string ShortcutDialog::comboText(const ShortcutCombo &combo) const {
    std::string t = shortcutToString(combo);
    return t.empty() ? I18n::get("shortcut.dialog.none") : t;
}

void ShortcutDialog::recomputeConflicts() {
    ShortcutRegistry reg;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (!m_rows[i].combo.empty()) {
            reg.set(m_rows[i].def.id, m_rows[i].combo.key, m_rows[i].combo.mods);
        }
    }
    for (size_t i = 0; i < m_rows.size(); ++i) {
        bool c = false;
        if (!m_rows[i].combo.empty()) {
            c = reg.conflictWith(m_rows[i].combo.key, m_rows[i].combo.mods,
                                 m_rows[i].def.id) != "";
        }
        m_rows[i].conflict = c;
        if (i < m_widgets.size()) m_widgets[i]->setConflict(c);
    }
}

void ShortcutDialog::rebuildComboLabels() {
    for (size_t i = 0; i < m_rows.size() && i < m_widgets.size(); ++i) {
        m_widgets[i]->setComboText(comboText(m_rows[i].combo));
    }
}

// Click on a row's combo box: clear the shortcut and start recording.
void ShortcutDialog::onRowComboClicked(RowWidget *w) {
    if (m_recording == w) {
        // Clicking the same box again cancels the recording.
        stopRecording();
        setError(nullptr);
        return;
    }
    stopRecording();
    if (w->index() >= 0 && (size_t)w->index() < m_rows.size()) {
        m_rows[w->index()].combo = ShortcutCombo();
    }
    m_recording = w;
    w->setRecording(true);
    w->take_focus();
    rebuildComboLabels();
    recomputeConflicts();
    setError(nullptr);
    updateHintText();   // show the recording prompt
}

// Click outside any combo box (or focus loss): cancel the recording.
void ShortcutDialog::onRowBlur(RowWidget *w) {
    if (m_recording != w) return;
    stopRecording();
    setError(nullptr);
}

void ShortcutDialog::stopRecording() {
    if (m_recording) {
        m_recording->setRecording(false);
        m_recording = nullptr;
        updateHintText();   // back to the Filter hint
    }
}

void ShortcutDialog::onRowComboChanged(RowWidget *w, const ShortcutCombo &combo) {
    if (m_recording != w) return;
    const char *err = shortcutValidate(combo);
    if (err) {
        setError(err);
        return;   // keep recording
    }
    // If another action already uses this combo, clear it (set to
    // none) so every shortcut stays unique; the new binding wins.
    if (w->index() >= 0 && (size_t)w->index() < m_rows.size()) {
        const std::string &myId = m_rows[w->index()].def.id;
        for (auto &other : m_rows) {
            if (other.def.id != myId && other.combo == combo) {
                other.combo = ShortcutCombo();
            }
        }
    }
    stopRecording();
    if (w->index() >= 0 && (size_t)w->index() < m_rows.size()) {
        m_rows[w->index()].combo = combo;
    }
    rebuildComboLabels();
    recomputeConflicts();
    setError(nullptr);
}

void ShortcutDialog::setError(const char *text) {
    m_errText = text ? text : "";
    updateHintText();
}

// The in-input hint shows, in priority order: an error (red), the
// recording prompt, or the plain "Filter" label.
void ShortcutDialog::updateHintText() {
    if (!m_hintLabel) return;
    if (!m_errText.empty()) {
        m_hintLabel->label(m_errText.c_str());
        m_hintLabel->labelcolor(fl_rgb_color(200, 30, 30));
    } else if (m_recording) {
        m_hintLabel->label(I18n::get("shortcut.dialog.recording"));
        m_hintLabel->labelcolor(m_theme ? m_theme->colors().textPrimary
                                        : FL_BLACK);
    } else {
        m_hintLabel->label(I18n::get("shortcut.dialog.filter"));
        m_hintLabel->labelcolor(m_theme ? m_theme->colors().textSecondary
                                        : fl_rgb_color(140, 140, 140));
    }
    m_hintLabel->redraw();
}

void ShortcutDialog::finishOk() {
    for (const auto &st : m_rows) {
        if (m_cfg) m_cfg->setShortcut(st.def.id.c_str(), shortcutToString(st.combo));
    }
    user_data(reinterpret_cast<void *>((intptr_t)1));
    hide();
}

void ShortcutDialog::cbOk(Fl_Widget * /*w*/, void *data) {
    auto *self = static_cast<ShortcutDialog *>(data);
    if (self) self->finishOk();
}

void ShortcutDialog::cbCancel(Fl_Widget * /*w*/, void *data) {
    auto *self = static_cast<ShortcutDialog *>(data);
    if (self) self->hide();
}

// Restore Defaults: every row back to its built-in combo.
void ShortcutDialog::resetToDefaults() {
    stopRecording();
    for (auto &st : m_rows) {
        st.combo.key = st.def.defKey;
        st.combo.mods = st.def.defMods;
    }
    rebuildComboLabels();
    recomputeConflicts();
    setError(nullptr);
}

void ShortcutDialog::cbReset(Fl_Widget * /*w*/, void *data) {
    auto *self = static_cast<ShortcutDialog *>(data);
    if (self) self->resetToDefaults();
}


bool ShortcutDialog::runModal() {
    position((Fl::w() - w()) / 2, (Fl::h() - h()) / 2);
    m_okBtn->when(FL_WHEN_RELEASE);
    m_cancelBtn->when(FL_WHEN_RELEASE);
    user_data(reinterpret_cast<void *>((intptr_t)0));
    show();

    // Note: no WS_EX_APPWINDOW here. border(0) windows carry
    // WS_EX_TOOLWINDOW, which keeps the dialog off the taskbar (that
    // style wins over APPWINDOW), matching every other modal dialog.

    while (shown()) Fl::wait();
    return reinterpret_cast<intptr_t>(user_data()) != 0;
}


