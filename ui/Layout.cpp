// Layout.cpp - runtime definitions for the shared UI bar/button heights.
#include "ui/Layout.h"
#include "core/Config.h"

int gBarH     = 32;   // bars WITH bordered buttons (title/find/go-to/button bars)
int gBtnH     = 24;   // unified button height
int gTextBarH = 24;   // borderless text bars (menu/toolbar/status)

int TITLE_H    = 24;
int MENU_H     = 24;
int SCRIPTBAR_H  = 24;
int FINDBAR_H  = 64;
int GOTOBAR_H  = 32;
int STATUS_H   = 24;

// Initialize every bar/button height from settings.ini (bar_height /
// btn_height / text_bar_height). Called once at startup by all three
// processes BEFORE any widget is created, so the whole UI picks up the
// configured rhythm.
void initLayoutHeights(const Config &cfg) {
    gBarH     = cfg.getBarHeight();
    gBtnH     = cfg.getBtnHeight();
    gTextBarH = cfg.getTextBarHeight();
    TITLE_H   = gTextBarH;
    FINDBAR_H = 2 * gBarH;
    GOTOBAR_H = gBarH;
    MENU_H    = gTextBarH;
    SCRIPTBAR_H = gTextBarH;
    STATUS_H  = gTextBarH;
}
