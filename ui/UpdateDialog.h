// UpdateDialog.h - the "Help > Check for Updates" page.
//
// One modal window holds the whole flow: current state, source picker,
// re-check, changelog, download progress and the update action. It replaces
// the old chain of "ConfirmDialog for the verdict + a second modal progress
// window", which could not show a changelog, could not switch source, and
// could not admit that its verdict came from a stale cache.
//
// Threading: the check and the download run on detached worker threads and
// marshal back with Fl::awake. `m_alive` is cleared when the window closes, so
// a late callback drops its result instead of touching a destroyed dialog or
// launching the updater script behind the user's back.
#pragma once

#include "DialogBase.h"

#include <FL/Fl.H>

#include <functional>
#include <memory>
#include <string>

#include "core/Updater.h"

class Theme;
class Config;
class HoverButton;
class Fl_Box;
class Fl_Group;
class Fl_Progress;
class Fl_Text_Buffer;
class ThemedTextDisplay;
class SettingsChoice;

class UpdateDialog : public DialogBase {
public:
    // confirmUnsaved runs before the process is handed to the updater script;
    // returning false cancels the update.
    UpdateDialog(const Theme *theme, int uiFontSize, Config *cfg,
                 std::function<bool()> confirmUnsaved);
    ~UpdateDialog() override;

    // Modal run loop (same shape as ConfirmDialog::run()). Kicks the first
    // network check once the window is visible.
    void run();

private:
    void startCheck();
    void applyCheckResult(const UpdateCheckResult &r);
    void startDownload();

    void setStatus(const char *text, Fl_Color colour);
    void setBusy(bool busy);
    void setProgress(double frac, const std::string &text);
    void showNotes(const std::string &text);
    void finishFailure(const std::string &message);
    Fl_Color hintColour() const;

    void requestClose();

    static void cbRefresh(Fl_Widget *, void *);
    static void cbDownload(Fl_Widget *, void *);
    static void cbSource(Fl_Widget *, void *);
    static void cbClose(Fl_Widget *, void *);

    void onCaptionClose() FL_OVERRIDE { requestClose(); }

    Config                *m_cfg  = nullptr;
    std::function<bool()>  m_confirmUnsaved;
    UpdateManifest         m_manifest;      // what is currently displayed
    bool                   m_busy = false;  // a worker thread owns the UI
    std::shared_ptr<bool>  m_alive = std::make_shared<bool>(true);

    Fl_Box          *m_status    = nullptr;
    Fl_Box          *m_curVer    = nullptr;
    Fl_Box          *m_mfVer     = nullptr;
    SettingsChoice  *m_source    = nullptr;
    ThemedTextDisplay *m_notes   = nullptr;
    Fl_Text_Buffer  *m_notesBuf  = nullptr;
    Fl_Group        *m_progRow   = nullptr;
    Fl_Box          *m_progText  = nullptr;
    Fl_Progress     *m_bar       = nullptr;
    HoverButton     *m_btnRefresh  = nullptr;
    HoverButton     *m_btnDownload = nullptr;
    HoverButton     *m_btnClose    = nullptr;

    // Fl_Widget::label() keeps the pointer, it does not copy - every label we
    // build at run time needs a member that outlives the call.
    std::string m_statusBuf;
    std::string m_curVerBuf;
    std::string m_mfVerBuf;
    std::string m_progTextBuf;
};
