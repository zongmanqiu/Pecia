// BandColourSource.h - lets a hand-drawn widget tell the window frame what
// colour it shows at a given point.
//
// Why this exists: the 6px non-client band (and the 1px border) must be the
// same colour as the client pixel it abuts, otherwise the window looks like it
// has a coloured strip inside its border. The band is painted in WM_NCPAINT,
// long before/after the client area is drawn, so it cannot read the screen -
// it walks the widget tree instead. Controls drawn by FLTK boxes are easy
// (box() + color()), but anything with a hand-written draw() (SidePanel's tab
// strip, Editor's text area using a style-table background) knows its colour
// only itself. Those implement this interface.
//
// Deliberately a standalone header with no FLTK / windows.h dependency so that
// editor/Editor.h can inherit it without dragging the Win32 headers in.
#pragma once

namespace windowChrome {

class BandColourSource {
public:
    virtual ~BandColourSource() = default;

    // (x, y) are in THIS widget's own coordinate space (the caller subtracts
    // the widget's x()/y() before asking).
    // Return false to let the caller keep looking at what sits behind you.
    virtual bool bandColourAt(int x, int y, unsigned int *colour) const = 0;
};

}  // namespace windowChrome
