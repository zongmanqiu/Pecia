// SettingsNav.h - the vertical page switcher inside Settings > Options.
//
// The reference is the main window's own tab strip (ui/TitleBar.cpp):
// self-drawn, self hit-tested, active cell in highlight1 - FLTK's Fl_Tabs can
// only put its tabs on top, and a browser-style settings page wants them on
// the left. Mouse-only by necessity: Fl_Widget::take_focus() is not virtual in
// FLTK 1.4, and a childless group can never hold the keyboard focus, so there
// is no honest Tab/arrow path to build here.
//
// Being a CHILD of the dialog, its cells are measured from its own x()/y():
// child coordinates here are absolute, which is the trap m038/m039 both hit
// when a window's layout was reused inside another window.
#pragma once

#include <FL/Fl_Group.H>

#include <functional>
#include <string>
#include <vector>

class Theme;

class SettingsNav : public Fl_Group {
public:
    SettingsNav(int x, int y, int w, int h, const Theme *theme, int fontSize);

    // The page names Options uses. Living here keeps the key list in one
    // place: the rail width, the labels and the dialog's page count all read
    // it, and two copies is how a rail gets laid out narrower than what it
    // clips.
    static std::vector<std::string> pageLabels();
    // Column width those labels need at this font size (padding included).
    static int widthFor(const std::vector<std::string> &labels, int fontSize);

    void setItems(const std::vector<std::string> &labels);
    void setActive(int i);
    int  active() const { return m_active; }
    int  itemCount() const { return (int)m_labels.size(); }
    void setTheme(const Theme *theme, int fontSize);

    // Which theme key the rail paints (the menu bar's background3). Asked
    // here instead of at each call site so the ctor, setTheme() and draw()
    // cannot drift apart after a theme switch.
    static Fl_Color railColor(const Theme *theme);
    // Label tone for a row. Both states answer text1: the active row is
    // already told apart by its cell fill, and a secondary tone on the
    // inactive ones reads as "disabled".
    Fl_Color labelColor(bool active) const;
    // Painted cell height: the slot minus the air between rows. The hit band
    // stays the full slot, so the gap never becomes dead space.
    int  cellHeight() const;

    void setOnSelect(std::function<void(int)> cb) { m_onSelect = std::move(cb); }

    // Row geometry in absolute window coordinates (the gate asserts these).
    int  itemHeight() const { return m_itemH; }
    int  rowIndexAt(int px, int py) const;      // -1 when it hits nothing

    int  handle(int event) FL_OVERRIDE;
    void draw() FL_OVERRIDE;
    void resize(int X, int Y, int W, int H) FL_OVERRIDE;

private:
    std::vector<std::string> m_labels;
    std::function<void(int)> m_onSelect;
    const Theme *m_theme = nullptr;
    int  m_fontSize = 16;
    int  m_active   = 0;
    int  m_itemH    = 24;
};
