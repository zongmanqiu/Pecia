// test_ui_smoke.cpp - construction-level UI smoke test.
//
// Purpose: catch "the window crashes when you open it" regressions without
// needing a human to click or a running GUI. Each window is OPENED through
// its real public path with a smoke-mode flag that prevents the internal
// modal `while(shown()) Fl::wait()` loop from blocking. Because a crash
// (access violation) aborts the process, reaching the CHECK after each
// window proves construction + layout + show ran without faulting.
//
// This would have caught the ShortcutDialog child(0) crash and the
// ParamDialog close-button overflow bug.
//
// Requires FLTK + the same sources the main target links, and the resources
// the dialogs read (lang files via exe-dir).
#include "test_assert.h"

#include "core/Config.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "core/ShortcutCore.h"
#include "script/LuaParamParser.h"
#include "ui/Layout.h"
#include "ui/SmokeTest.h"
#include "ui/AboutDialog.h"
#include "ui/InfoWindow.h"

#include "ui/SettingsDialog.h"
#include "ui/SettingsNav.h"   // m040 S3: the page gate drives the switcher itself
#include "ui/ConfirmDialog.h"
#include "ui/ParamDialog.h"
#include "ui/FontDialog.h"
#include "ui/ShortcutDialog.h"

#include <FL/Fl.H>
#include <FL/Fl_Preferences.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Button.H>
#include <cstdlib>   // getenv(PECIA_UI_SHOT)
#include <chrono>

#include <cstdio>
#include <vector>
#include <string>
#include <filesystem>
#include <windows.h>

// ---------------------------------------------------------------------------
// Data-flow test seam. Declared a friend of the dialogs so a smoke test can
// set control values and drive the commit path (saveTo/collectAndOk/finishOk)
// without showing the window or needing real mouse/keyboard input.
// ---------------------------------------------------------------------------
struct UiSmokeAccess {
    // SettingsDialog -----------------------------------------------------
    static void settingsSetTabWidth(SettingsDialog &d, int idx) { d.m_tabWidthChoice->value(idx); }
    static void settingsSetAutoIndent(SettingsDialog &d, bool on) { d.m_autoIndentChk->value(on ? 1 : 0); }
    static void settingsSetMultiTab(SettingsDialog &d, bool on) { d.m_multiTabChk->value(on ? 1 : 0); }
    static bool settingsSaveTo(SettingsDialog &d, Config &cfg) { return d.saveTo(cfg); }
    // m040: endpoint / model / key are configured here and nowhere else.
    static void settingsSetAiEndpoint(SettingsDialog &d, const char *v) { d.m_aiEndpoint->value(v); }
    static void settingsSetAiContext(SettingsDialog &d, const char *v) { d.m_aiContext->value(v); }
    static void settingsSetAiModel(SettingsDialog &d, const char *v) { d.m_aiModel->value(v); }
    static void settingsSetAiKey(SettingsDialog &d, const char *v) { d.m_aiKey->value(v); }
    static const char *settingsAiEndpointText(const SettingsDialog &d) { return d.m_aiEndpoint->value(); }
    static int settingsPageOfSearchUrl(const SettingsDialog &d) { return settingsPageOf(d, d.m_searchUrlInput); }
    static int settingsPageOfAiEndpoint(const SettingsDialog &d)  { return settingsPageOf(d, d.m_aiEndpoint); }
    static int settingsPageOfAutoSave(const SettingsDialog &d)    { return settingsPageOf(d, d.m_autoSaveChoice); }
    static int settingsPageOfMultiTab(const SettingsDialog &d)    { return settingsPageOf(d, d.m_multiTabChk); }
    static int settingsPageOf(const SettingsDialog &d, const Fl_Widget *w) {
        for (size_t i = 0; i < d.m_pages.size(); ++i) {
            Fl_Group *pg = d.m_pages[i];
            if (!pg) continue;
            for (int k = 0; k < pg->children(); ++k)
                if (pg->child(k) == w) return (int)i;
        }
        return -1;
    }

    // ParamDialog --------------------------------------------------------
    static void paramSetText(ParamDialog &d, size_t i, const char *v) { if (i < d.m_inputs.size()) d.m_inputs[i]->value(v); }
    static void paramSetChoice(ParamDialog &d, size_t i, int idx) { if (i < d.m_choices.size()) d.m_choices[i]->value(idx); }
    static void paramSetCheck(ParamDialog &d, size_t i, bool on) { if (i < d.m_checks.size()) d.m_checks[i]->value(on ? 1 : 0); }
    static void paramCommit(ParamDialog &d) { ParamDialog::collectAndOk(&d); }
    static const std::vector<std::string> &paramResults(const ParamDialog &d) { return d.m_values; }

    // ShortcutDialog -----------------------------------------------------
    // Force a row's in-memory combo (the dialog uses this before finishOk).
    static void shortcutSetCombo(ShortcutDialog &d, size_t row, int key, unsigned mods) {
        if (row < d.m_rows.size()) { d.m_rows[row].combo.key = key; d.m_rows[row].combo.mods = mods; }
    }
    static void shortcutCommit(ShortcutDialog &d) { d.finishOk(); }
    static std::string shortcutComboText(const ShortcutDialog &d, size_t row) {
        if (row >= d.m_rows.size()) return std::string();
        return d.comboText(d.m_rows[row].combo);
    }
    static size_t shortcutRowCount(const ShortcutDialog &d) { return d.m_rows.size(); }

    // FontDialog ----------------------------------------------------------
    static void fontSetSearch(FontDialog &d, const char *q) { d.m_search->value(q); d.rebuildList(); }
    static void fontSetMono(FontDialog &d, bool on) { d.m_monoBox->value(on ? 1 : 0); d.rebuildList(); }
    static size_t fontAllCount(const FontDialog &d) { return d.m_all.size(); }
    static size_t fontShownCount(const FontDialog &d) { return d.m_shown.size(); }
    static void fontSelectIndex(FontDialog &d, size_t i) { d.selectLine((int)i + 1); }
    static void fontCommit(FontDialog &d) { FontDialog::cbOk(nullptr, &d); }
    static const std::string &fontChosen(const FontDialog &d) { return d.m_chosen; }
};

namespace {

// Temporary config directory so the data-flow tests never touch the real
// settings.ini. Returns a canonical string usable by Config.
std::filesystem::path testConfigDir() {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                ("pecia_ui_smoke_" + std::to_string(::GetCurrentProcessId()));
    std::error_code ec;
    // Wipe first. The name is keyed on the PID, and PIDs get recycled: without
    // this, a leftover settings.ini from an earlier run hands the test
    // non-default values, and "change the settings" silently becomes "no
    // change" (runDataFlowTest's settingsSaveTo then returns false).
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// One-time FLTK + app init before constructing any window (mirrors the
// startup sequence in main_*.cpp).
//
// A dialog that only "didn't crash" is half the story: it can also build but
// lay out to nothing (0-size window, no children), which to the user looks
// exactly like "the menu item does nothing". checkDialog pins the other half.
// PECIA_UI_SHOT=1 shows the Options dialog and holds each page on screen for
// a second and a half, so an external PrintWindow pass can take the pixels of
// every page. Off by default: the gate must not depend on a window existing.
static void shotOptionsPages(SettingsDialog &dlg) {
    for (int i = 0; i < dlg.pageCount(); ++i) {
        SettingsNav *nav = dlg.nav();
        if (nav) {
            Fl::e_x = nav->x() + 5;
            Fl::e_y = nav->y() + nav->itemHeight() * i + 2;
            Fl::e_keysym = FL_Button + FL_LEFT_MOUSE;
            nav->handle(FL_PUSH);
        }
        dlg.show();
        dlg.redraw();
        // Fl::wait(t) comes back as soon as ANY event lands, so holding a
        // page on screen needs a deadline, not one long wait.
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
        while (std::chrono::steady_clock::now() < until) Fl::wait(0.1);
    }
    dlg.hide();
}

static void checkDialog(const Fl_Widget &w, const char *what) {
    const bool sized = w.w() > 0 && w.h() > 0;
    const auto *g = dynamic_cast<const Fl_Group *>(&w);
    const bool hasKids = g && g->children() > 0;
    if (!sized || !hasKids)
        fprintf(stderr, "  [%s] w=%d h=%d children=%d\n", what, w.w(), w.h(),
                hasKids ? g->children() : 0);
    CHECK(sized);
    CHECK(hasKids);
}

void runOpenTest() {
    Config cfg;
    initLayoutHeights(cfg);
    I18n::load("en");        // 只加载一次：失败不重试，语言包缺失要暴露出来
    Theme theme;
    theme.load(cfg);
    const int fs = 16;

    std::fprintf(stderr, "[uismoke] shown-dialogs\n");
    // ── Settings / Options ──
    {
        SettingsDialog dlg(520, 480, "smoke-settings", &theme, fs, 16);
        checkDialog(dlg, "settings");
    }

    // ── ExtensionsDialog (Open-with chooser; previously not covered) ──
    {
        SettingsDialog dlg(520, 480, "smoke-ext", &theme, fs, 16);
        dlg.openExtensionsForTest();
        checkDialog(dlg, "extensions");
    }

    // ── Confirm (3 / 2 / 1 buttons) ──
    {
        ConfirmDialog dlg("t", "msg", "Yes", "No", "Cancel", &theme, fs);
        checkDialog(dlg, "confirm/3");
    }
    {
        ConfirmDialog dlg("t", "msg", "OK", "Cancel", nullptr, &theme, fs);
        checkDialog(dlg, "confirm/2");
    }
    {
        ConfirmDialog dlg("t", "msg", "OK", nullptr, nullptr, &theme, fs);
        checkDialog(dlg, "confirm/1");
    }

    // ── Param (multi-field incl. text/checkbox/choice, and a single short
    //    field that exposed the close-button overflow) ──
    {
        std::vector<LuaParam> params;
        LuaParam p1; p1.name = "n"; p1.defValue = "10";
        p1.description = "Count";
        LuaParam p2; p2.name = "flag"; p2.isCheckbox = true;
        p2.choices = {"on", "off"}; p2.defValue = "on"; p2.description = "Enabled";
        LuaParam p3; p3.name = "mode"; p3.choices = {"a", "b", "c"};
        p3.defValue = "b"; p3.description = "Mode";
        ParamDialog dlg("smoke-param", {p1, p2, p3}, &theme, fs);
        checkDialog(dlg, "param/3");
        ParamDialog dlg2("smoke-param2", {p1}, &theme, fs);
        checkDialog(dlg2, "param/1");
    }

    // ── Shortcut (exercises layoutRows()/repositionRows()/fitButtonRow) ──
    {
        std::vector<ShortcutDialogRow> rows;
        ShortcutDialogRow a;
        a.id = "win.pin"; a.name = "Pin"; a.group = SG_WINDOW;
        a.defKey = 0; a.defMods = 0;
        ShortcutDialogRow b;
        b.id = "win.minimize"; b.name = "Minimize"; b.group = SG_WINDOW;
        b.defKey = FL_F + 9; b.defMods = FL_CTRL;
        ShortcutDialog dlg(520, 560, "smoke-shortcut", &cfg, &theme, fs);
        dlg.setRows(rows);
        checkDialog(dlg, "shortcut");
    }

    // ── Info (doc statistics; smoke mode skips the blocking loop) ──
    {
        InfoWindow iw;
        iw.setTheme(&theme, fs);
        iw.setTitle("Stats");
        iw.setWidth(320);
        iw.setRows({{"Chars", "123"}, {"Words", "45"}, {"Lines", "6"}});
        iw.show();
        // InfoWindow owns its Fl_Window internally and hides it in smoke mode,
        // so there is no sizing to inspect here: surviving show() is the check
        // (a fault would abort the process). No checkable value to assert.
        (void)0;
    }

    // ── About (smoke mode skips the blocking loop) ──
    // showAboutDialog() owns its window internally and, in smoke mode, returns
    // right after hiding it - there is no object left to measure. Surviving the
    // call IS the assertion (an access violation aborts the process).
    showAboutDialog(&theme, fs);
    // No object survives the call to inspect; surviving it IS the assertion.
    (void)0;
}

// Data-flow round-trips: exercise the commit paths that a modal OK click
// would drive, but without showing/blocking the dialogs (UiSmokeAccess is a
// test friend). Uses an isolated temp Config so the real settings.ini is never
// touched.
void runDataFlowTest() {
    Config cfg(testConfigDir());                        // isolated config dir
    initLayoutHeights(cfg);
    Theme theme;
    theme.load(cfg);
    const int fs = 16;

    std::fprintf(stderr, "[uismoke] roundtrip\n");
    // ── SettingsDialog: loadFrom -> change -> saveTo round-trip ──
    {
        SettingsDialog dlg(520, 480, "flow-settings", &theme, fs, 16);
        // Populate controls from config (all defaults now).
        dlg.loadFrom(cfg);

        // Change several controls and commit.
        UiSmokeAccess::settingsSetTabWidth(dlg, 1);       // 4 space (index 1)
        UiSmokeAccess::settingsSetAutoIndent(dlg, true);
        UiSmokeAccess::settingsSetMultiTab(dlg, true);
        bool changed = UiSmokeAccess::settingsSaveTo(dlg, cfg);
        CHECK(changed);

        // Read back through the same Config object (in-memory + ini).
        CHECK_EQ(cfg.getTabWidth(), 4);
        CHECK(cfg.getAutoIndent());
        CHECK(cfg.getMultiTab());
    }

    std::fprintf(stderr, "[uismoke] ai-roundtrip\n");
    // ── m040: AI endpoint / model / key round-trip through Options ──
    {
        SettingsDialog dlg(520, 480, "flow-ai", &theme, fs, 16);
        dlg.loadFrom(cfg);
        UiSmokeAccess::settingsSetAiEndpoint(dlg, " https://example.test/v1 ");
        UiSmokeAccess::settingsSetAiModel(dlg, "  some-model  ");
        UiSmokeAccess::settingsSetAiKey(dlg, " secret-key ");
        UiSmokeAccess::settingsSetAiContext(dlg, "7");
        CHECK(UiSmokeAccess::settingsSaveTo(dlg, cfg));

        char buf[512];
        cfg.getAiEndpoint(buf, sizeof(buf), "");
        // Hand-typed values are trimmed: a trailing space in a URL is a
        // silent 404, and this is the same trim the deleted dialog did.
        CHECK(std::string(buf) == "https://example.test/v1");
        cfg.getAiModel(buf, sizeof(buf), "");
        CHECK(std::string(buf) == "some-model");
        cfg.getAiKey(buf, sizeof(buf), "");
        CHECK(std::string(buf) == "secret-key");
        // context turns round-trip; out-of-range values are clamped on save
        CHECK(cfg.getAiContextTurns() == 7);
        UiSmokeAccess::settingsSetAiContext(dlg, "9999");
        UiSmokeAccess::settingsSaveTo(dlg, cfg);
        CHECK(cfg.getAiContextTurns() == 100);

        // Saving the same values again must report "nothing changed",
        // otherwise every Options close re-themes the whole window.
        SettingsDialog dlg2(520, 480, "flow-ai2", &theme, fs, 16);
        dlg2.loadFrom(cfg);
        CHECK(!UiSmokeAccess::settingsSaveTo(dlg2, cfg));
        // and the loadFrom side really reads the stored values back
        CHECK(std::string(UiSmokeAccess::settingsAiEndpointText(dlg2))
              == "https://example.test/v1");
        UiSmokeAccess::settingsSetAiKey(dlg2, "another");
        CHECK(UiSmokeAccess::settingsSaveTo(dlg2, cfg));
        cfg.getAiKey(buf, sizeof(buf), "");
        CHECK(std::string(buf) == "another");
    }
    // The CHECKs above are this section's assertions; no trailing placeholder.
    (void)0;

    std::fprintf(stderr, "[uismoke] pages\n");
    // ── m040 S3: Options is a vertical page switcher, not one long scroll ──
    {
        SettingsDialog dlg(520, 480, "flow-pages", &theme, fs, 16);
        dlg.loadFrom(cfg);

        CHECK(dlg.pageCount() == 4);
        CHECK(dlg.nav() != nullptr);
        CHECK(dlg.nav()->itemCount() == 4);

        // Nothing may sit directly in the content area except the switcher
        // and the pages: a control created outside beginPage() would simply
        // be invisible, and this is the count that catches it.
        Fl_Group *content = dlg.contentGroup();
        CHECK(content != nullptr);
        int unassigned = 0;
        for (int i = 0; content && i < content->children(); ++i) {
            Fl_Widget *kid = content->child(i);
            if (kid == (Fl_Widget *)dlg.nav()) continue;
            bool isPage = false;
            for (int k = 0; k < dlg.pageCount(); ++k)
                if (dlg.pageAt(k) == kid) { isPage = true; break; }
            if (!isPage) ++unassigned;
        }
        CHECK(unassigned == 0);

        // Rows are inside their own page - the pages are children of the
        // dialog, so their rows carry ABSOLUTE coordinates (m038's trap).
        int outside = 0, rows = 0;
        for (int k = 0; k < dlg.pageCount(); ++k) {
            Fl_Group *pg = dlg.pageAt(k);
            if (!pg) continue;
            CHECK(pg->children() > 0);
            for (int i = 0; i < pg->children(); ++i) {
                Fl_Widget *r = pg->child(i);
                ++rows;
                if (r->x() < pg->x() || r->y() < pg->y()
                    || r->x() + r->w() > pg->x() + pg->w()
                    || r->y() + r->h() > pg->y() + pg->h()) ++outside;
            }
        }
        CHECK(outside == 0);
        CHECK(rows >= 20);

        // Membership, not just counts: the search-engine row is on the first
        // page and the API rows on the last. Dumping everything into page 0
        // keeps every count above true and fails here.
        CHECK(UiSmokeAccess::settingsPageOfSearchUrl(dlg) == 0);
        CHECK(UiSmokeAccess::settingsPageOfAiEndpoint(dlg) == 3);
        CHECK(UiSmokeAccess::settingsPageOfAutoSave(dlg) == 1);
        CHECK(UiSmokeAccess::settingsPageOfMultiTab(dlg) == 2);

        // The rail is chrome, not a floating column: flush with the window's
        // left edge, the full height of the content area, and the page column
        // starts just past its right rule.
        CHECK(dlg.nav()->x() == 0);
        CHECK(dlg.nav()->y() == dlg.contentGroup()->y());
        CHECK(dlg.nav()->h() == dlg.contentGroup()->h());
        CHECK(dlg.pageAt(0)->x() > dlg.nav()->x() + dlg.nav()->w());
        CHECK(dlg.pageAt(0)->x() - (dlg.nav()->x() + dlg.nav()->w()) <= 16);
        // The rail must be wide enough for its own labels: it measures them,
        // so a column narrower than the widest name means clipped text.
        CHECK(dlg.nav()->w() >= 96);

        // Chrome, not a floating column, and no white band above it: the
        // content area starts under the title bar, so the rail's first pixel
        // is the title bar's last one.
        CHECK(dlg.contentGroup()->y() == TITLE_H);

        // The rail answers the menu bar's theme key, and its labels answer
        // text1 in both states - a hint tone on the inactive rows would read
        // as "disabled", and a hardcoded colour here would survive a theme
        // switch only by luck.
        CHECK(dlg.nav()->color() == SettingsNav::railColor(&theme));
        CHECK(SettingsNav::railColor(&theme) == theme.colors().background3);
        CHECK(dlg.nav()->labelColor(false) == theme.colors().text1);
        CHECK(dlg.nav()->labelColor(true) != theme.colors().text2);

        // Rows divide by air, not by ink: the painted cell is shorter than
        // the slot, but the slot stays clickable down to its last pixel, so
        // the gap is not dead space between two buttons.
        CHECK(dlg.nav()->cellHeight() < dlg.nav()->itemHeight());
        Fl::e_x = dlg.nav()->x() + 5;
        Fl::e_y = dlg.nav()->y() + dlg.nav()->itemHeight() - 1;
        Fl::e_keysym = FL_Button + FL_LEFT_MOUSE;
        CHECK(dlg.nav()->handle(FL_PUSH) == 1);
        CHECK(dlg.nav()->active() == 0);

        // m041: every field in Options is square like the main window's own
        // inputs. A bare Fl_Input keeps FLTK's rounded box, so this counts
        // the exceptions across ALL pages instead of trusting that one widget
        // on the AI page happened to be changed.
        int inputs = 0, rounded = 0, page3Buttons = 0;
        for (int k = 0; k < dlg.pageCount(); ++k) {
            Fl_Group *pg = dlg.pageAt(k);
            for (int i = 0; i < pg->children(); ++i) {
                Fl_Widget *r = pg->child(i);
                if (Fl_Input *in = dynamic_cast<Fl_Input *>(r)) {
                    ++inputs;
                    if (in->box() != FL_BORDER_BOX) ++rounded;
                }
                // A checkbox is a Fl_Button by inheritance; the thing that
                // was removed was a push button, so only count those.
                if (k == 3 && dynamic_cast<Fl_Button *>(r)
                    && !dynamic_cast<Fl_Check_Button *>(r)) ++page3Buttons;
            }
        }
        CHECK(inputs >= 4);
        CHECK(rounded == 0);
        CHECK(page3Buttons == 0);

        // The field is "API URL", not a bare "URL", in every language.
        CHECK(std::string(I18n::get("ai.endpoint")).compare(0, 4, "API ") == 0);

        // Only the active page is shown.
        CHECK(dlg.pageAt(0)->visible());
        CHECK(!dlg.pageAt(1)->visible());
        CHECK(!dlg.pageAt(3)->visible());

        // The switcher's own hit test drives it (no mouse injection, no
        // foreground window: the push is fed straight to the widget).
        Fl::e_x = dlg.nav()->x() + 5;
        Fl::e_y = dlg.nav()->y() + dlg.nav()->itemHeight() * 3 + 2;
        Fl::e_keysym = FL_Button + FL_LEFT_MOUSE;
        CHECK(dlg.nav()->handle(FL_PUSH) == 1);
        CHECK(dlg.nav()->active() == 3);
        CHECK(dlg.pageAt(3)->visible());
        CHECK(!dlg.pageAt(0)->visible());

        // Below the last row there is nothing to select.
        Fl::e_y = dlg.nav()->y() + dlg.nav()->itemHeight() * 99;
        const int before = dlg.nav()->active();
        dlg.nav()->handle(FL_PUSH);
        CHECK(dlg.nav()->active() == before);

        // Values survive a page switch: writing on page 3, switching to page 0
        // and saving must persist BOTH - the controls are never rebuilt.
        UiSmokeAccess::settingsSetAiKey(dlg, "cross-page-key");
        Fl::e_y = dlg.nav()->y() + 2;
        dlg.nav()->handle(FL_PUSH);
        CHECK(dlg.nav()->active() == 0);
        UiSmokeAccess::settingsSetTabWidth(dlg, 1);
        CHECK(UiSmokeAccess::settingsSaveTo(dlg, cfg));
        char aiBuf[512];
        cfg.getAiKey(aiBuf, sizeof(aiBuf), "");
        CHECK(std::string(aiBuf) == "cross-page-key");
        CHECK_EQ(cfg.getTabWidth(), 4);

        std::fprintf(stderr, "options pages: count=%d rows=%d navW=%d h=%d\n",
                     dlg.pageCount(), rows, dlg.nav()->w(), dlg.h());

        if (getenv("PECIA_UI_SHOT")) shotOptionsPages(dlg);
    }

    std::fprintf(stderr, "[uismoke] param\n");
    // ── ParamDialog: set text/checkbox/choice -> commit -> results() ──
    {
        std::vector<LuaParam> params;
        LuaParam a; a.name = "n"; a.defValue = "10"; a.description = "Count";
        LuaParam b; b.name = "flag"; b.isCheckbox = true;
        b.choices = {"on", "off"}; b.defValue = "on";   // on=true off=false
        LuaParam c; c.name = "mode"; c.choices = {"a", "b", "c"};
        c.defValue = "b"; c.description = "Mode";
        params = {a, b, c};
        ParamDialog dlg("flow-param", params, &theme, fs);

        // User edits: text "7", uncheck the flag (off), choose "c".
        UiSmokeAccess::paramSetText(dlg, 0, "7");
        UiSmokeAccess::paramSetCheck(dlg, 0, false);      // off (only checkbox, index 0)
        UiSmokeAccess::paramSetChoice(dlg, 0, 2);         // "c"
        UiSmokeAccess::paramCommit(dlg);

        const auto &res = UiSmokeAccess::paramResults(dlg);
        CHECK_EQ((int)res.size(), 3);
        if (res.size() == 3) {
            CHECK(res[0] == "7");
            CHECK(res[1] == "off");      // checkbox off -> the off choice
            CHECK(res[2] == "c");        // raw option value, not the label
        }
    }
    // The CHECKs above are this section's assertions; no trailing placeholder.
    (void)0;

    // ── ShortcutDialog: set a row combo -> finishOk -> config shortcut ──
    {
        std::vector<ShortcutDialogRow> rows;
        ShortcutDialogRow a;
        a.id = "win.pin"; a.name = "Pin"; a.group = SG_WINDOW;
        a.defKey = 0; a.defMods = 0;
        rows.push_back(a);
        ShortcutDialog dlg(520, 560, "flow-shortcut", &cfg, &theme, fs);
        dlg.setRows(rows);

        // Bind Ctrl+Alt+P for win.pin and read it back from config.
        UiSmokeAccess::shortcutSetCombo(dlg, 0, 'p', FL_CTRL | FL_ALT);
        UiSmokeAccess::shortcutCommit(dlg);      // finishOk writes to cfg
        std::string s = cfg.getShortcut("win.pin");
        CHECK(!s.empty());
        CHECK(s == "Ctrl+Alt+P");
        CHECK(UiSmokeAccess::shortcutComboText(dlg, 0) == "Ctrl+Alt+P");
    }
    // The CHECKs above are this section's assertions; no trailing placeholder.
    (void)0;

    // ── FontDialog: real system enumeration + search + mono filter + commit.
    // Construction alone exercises Fl::set_fonts("*") and the attr/@-prefix
    // filters for every installed face.
    {
        FontDialog dlg(&theme, fs, "Consolas");
        CHECK(UiSmokeAccess::fontAllCount(dlg) > 0);

        // Search narrows the list; the default font must survive it on any
        // Windows box (Consolas ships with the OS).
        size_t all = UiSmokeAccess::fontAllCount(dlg);
        UiSmokeAccess::fontSetSearch(dlg, "consol");
        size_t narrowed = UiSmokeAccess::fontShownCount(dlg);
        CHECK(narrowed > 0);
        CHECK(narrowed < all);

        // Monospace filter keeps only fixed-pitch faces (GDI TEXTMETRIC).
        UiSmokeAccess::fontSetSearch(dlg, "");
        UiSmokeAccess::fontSetMono(dlg, true);
        CHECK(UiSmokeAccess::fontShownCount(dlg) > 0);
        UiSmokeAccess::fontSetMono(dlg, false);

        // Select the first hit and commit -> chosen() carries the name.
        UiSmokeAccess::fontSelectIndex(dlg, 0);
        UiSmokeAccess::fontCommit(dlg);
        CHECK(!UiSmokeAccess::fontChosen(dlg).empty());
    }
    // The CHECKs above are this section's assertions; no trailing placeholder.
    (void)0;
}

} // namespace

int main() {
    setvbuf(stderr, nullptr, _IONBF, 0);   // a crash must not swallow the last block
    // Portable: as with the Pecia entry point, forbid FLTK core from reading
    // and writing prefs outside the exe. Constructing any widget touches
    // Fl::option(), which would otherwise create fltk.org/fltk.prefs under
    // %APPDATA%/%ProgramData%. Must run before ANY FLTK call.
    Fl_Preferences::file_access(Fl_Preferences::APP_OK);
    Fl::visual(FL_DOUBLE | FL_RGB);

    // ── Startup sequence MUST mirror main.cpp ──────────────────────────────
    // These two were missing until 2026-10-23, and their absence is why this
    // test's windows looked nothing like the product: widget borders fell back
    // to FLTK's stock FL_BORDER_BOX (grey/black 1px) instead of the themed one,
    // and CJK faces were never enumerated, so Chinese labels rendered with a
    // fallback font. A smoke test that runs a different global environment than
    // the product cannot say anything about what the product looks like.
    //
    // Both steps are asserted, not just called: a silently no-op call would put
    // us right back where we started.
    Fl_Box_Draw_F *borderBefore = Fl::get_boxtype(FL_BORDER_BOX);
    const int fontCount = Fl::set_fonts("*");   // main.cpp:75
    theme_registerBorderBox();                  // main.cpp:81
    CHECK(fontCount > 100);                     // CJK faces really enumerated
    CHECK(Fl::get_boxtype(FL_BORDER_BOX) != borderBefore);  // themed border installed

    Fl::scheme("gtk+");
    ui::g_smokeMode = true;   // no blocking modal loops during the smoke test

    runOpenTest();
    runDataFlowTest();

    if (test::failCount() == 0) {
        printf("test_ui_smoke: all windows opened and data flows verified\n");
        return 0;
    }
    fprintf(stderr, "test_ui_smoke: %d/%d checks FAILED\n",
            test::failCount(), test::checkCount());
    return 1;
}
