// SettingsNav.cpp - see SettingsNav.h for why this self-draws.
#include "ui/SettingsNav.h"

#include "core/I18n.h"
#include "core/Theme.h"

#include <FL/Fl.H>
#include <FL/Fl_Window.H>   // FLTK 1.4: the cursor belongs to the window
#include <FL/fl_draw.H>

namespace {
const int CELL_PAD = 12;    // label padding inside a row
const int ROW_GAP  = 4;     // air between cells, not a drawn line
const int MIN_ROW  = 22;    // never let a small font make rows unclickable
const int MIN_W    = 96;    // a rail this narrow reads as a stub
const int MAX_W    = 170;   // longer translations clip instead of widening

// The tones used when no theme was handed over (tests construct the rail
// without one). One object, so the ctor / draw() / labelColor() cannot each
// invent their own fallback and disagree.
const ThemeColors &kFallbackTone()
{
    static const ThemeColors k;
    return k;
}
} // namespace

Fl_Color SettingsNav::railColor(const Theme *theme)
{
    return theme ? theme->colors().background3 : kFallbackTone().background3;
}

Fl_Color SettingsNav::labelColor(bool active) const
{
    const ThemeColors &tc = m_theme ? m_theme->colors() : kFallbackTone();
    // Both states answer the main text tone (same rule as the panel's tab
    // strip): a secondary tone on the inactive rows reads as "disabled".
    // On the filled row only the contrast pass runs, never a different key.
    return active ? fl_contrast(tc.text1, tc.highlight1) : tc.text1;
}

int SettingsNav::cellHeight() const
{
    const int h = m_itemH - ROW_GAP;
    return h > 1 ? h : 1;
}

std::vector<std::string> SettingsNav::pageLabels()
{
    static const char *const kKeys[] = {
        "settings.group_edit", "settings.group_save",
        "settings.group_ui_sys", "ai.section",
    };
    std::vector<std::string> out;
    for (const char *k : kKeys) out.emplace_back(I18n::get(k));
    return out;
}

int SettingsNav::widthFor(const std::vector<std::string> &labels, int fontSize)
{
    // fl_width() must not run before fl_font() (FLTK looks up size 0 and reads
    // fonts[-1]) - the font is selected here for the same reason.
    fl_font(FL_HELVETICA, fontSize);
    int need = MIN_W;
    for (const std::string &s : labels) {
        const int px = (int)(fl_width(s.c_str()) + 0.5f) + 2 * CELL_PAD;
        if (px > need) need = px;
    }
    if (need > MAX_W) need = MAX_W;
    return need;
}

SettingsNav::SettingsNav(int x, int y, int w, int h,
                         const Theme *theme, int fontSize)
    : Fl_Group(x, y, w, h), m_theme(theme), m_fontSize(fontSize) {
    box(FL_FLAT_BOX);
    color(railColor(theme));
    m_itemH = (fontSize + 10 > MIN_ROW) ? fontSize + 10 : MIN_ROW;
    // Fl_Group's ctor calls begin(); end() hands 'current' back to our parent.
    // Without it every widget the dialog creates next would land inside us.
    end();
}

void SettingsNav::setItems(const std::vector<std::string> &labels) {
    m_labels = labels;
    if (m_active >= (int)m_labels.size()) m_active = 0;
    redraw();
}

void SettingsNav::setActive(int i) {
    if (i < 0 || i >= (int)m_labels.size() || i == m_active) return;
    m_active = i;
    redraw();
}

void SettingsNav::setTheme(const Theme *theme, int fontSize) {
    m_theme    = theme;
    m_fontSize = fontSize;
    m_itemH = (fontSize + 10 > MIN_ROW) ? fontSize + 10 : MIN_ROW;
    color(railColor(theme));
    redraw();
}

int SettingsNav::rowIndexAt(int px, int py) const {
    if (px < x() || px >= x() + w()) return -1;
    const int bottom = y() + m_itemH * (int)m_labels.size();
    if (py < y() || py >= bottom) return -1;
    return (py - y()) / m_itemH;
}

int SettingsNav::handle(int event) {
    switch (event) {
    case FL_PUSH: {
        const int i = rowIndexAt(Fl::event_x(), Fl::event_y());
        if (i < 0) return 1;              // below the last row: swallow
        if (i != m_active) {
            m_active = i;
            redraw();
            if (m_onSelect) m_onSelect(i);
        }
        return 1;
    }
    case FL_ENTER:
        if (window()) window()->cursor(FL_CURSOR_HAND);
        return 1;
    case FL_LEAVE:
        if (window()) window()->cursor(FL_CURSOR_DEFAULT);
        return 1;
    default:
        return 0;
    }
}

void SettingsNav::resize(int X, int Y, int W, int H) {
    Fl_Group::resize(X, Y, W, H);
    redraw();
}

void SettingsNav::draw() {
    const ThemeColors &tc = m_theme ? m_theme->colors() : kFallbackTone();

    fl_push_clip(x(), y(), w(), h());
    // Menu-bar tone, edge to edge: no right-edge rule. The rail and the page
    // are told apart by the tone difference alone, the same way the side
    // panel's tool strip does it.
    fl_rectf(x(), y(), w(), h(), railColor(m_theme));

    fl_font(FL_HELVETICA, m_fontSize);
    const int cellH = cellHeight();
    for (size_t i = 0; i < m_labels.size(); ++i) {
        const int ry = y() + (int)i * m_itemH;
        const bool on = ((int)i == m_active);
        // Full-width cell, short by the air between rows: the active fill
        // reaches both edges but stops before the next label, so rows divide
        // by spacing instead of by an inked line.
        if (on) fl_rectf(x(), ry, w(), cellH, tc.highlight1);
        fl_color(labelColor(on));
        // Single line, centred in the cell: a label longer than the column
        // is clipped above rather than wrapped onto its neighbour.
        fl_draw(m_labels[i].c_str(), x() + CELL_PAD,
                ry + cellH / 2 + m_fontSize / 2 - 2);
    }

    fl_pop_clip();
}
