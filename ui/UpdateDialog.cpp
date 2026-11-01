// UpdateDialog.cpp - see UpdateDialog.h
#include "ui/UpdateDialog.h"

#include "ui/ThemeWidgets.h"
#include "ui/Layout.h"
#include "core/Config.h"
#include "core/I18n.h"
#include "core/Theme.h"
#include "core/Updater.h"

#include <FL/Fl_Box.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <thread>
#include <utility>

namespace {

const int MARGIN   = 16;
const int GAP      = 8;
const int NOTES_H  = 202;  // grew by the height of the old bottom progress row
const int kStatusH = 40;   // two wrapped lines
const int kGaugeW  = 120;  // fixed column: the status text always starts after it
const int kGaugeH  = 18;

// One Fl::awake hop. `alive` is cleared when the window closes, so a worker
// that finishes afterwards drops its result instead of writing into a destroyed
// dialog - or, worse, launching the updater script on a user who walked away.

template <typename T>
struct Job {
    std::shared_ptr<bool> alive;
    UpdateDialog        *self;
    T                    payload;
};

} // namespace

UpdateDialog::UpdateDialog(const Theme *theme, int uiFontSize, Config *cfg,
                           std::function<bool()> confirmUnsaved)
    : DialogBase(560, 0, I18n::get("update.title"), theme, uiFontSize, ModalDialog),
      m_cfg(cfg), m_confirmUnsaved(std::move(confirmUnsaved)) {
    static const ThemeColors kFallback;
    const ThemeColors &tc = theme ? theme->colors() : kFallback;
    const int rowH = gBtnH;
    const int W = 560;

    begin();
    initShell(I18n::get("update.title"));

    int y = contentTop() + 14;

    // No news is just the text: the gauge only exists while something is
    // transferring (setTransferMode), so an idle row is a full-width line.
    // Fl_Progress draws label(), not value(), so an empty label gives a clean
    // gauge and the percentage stays in the text beside it.
    m_statusY = y;
    m_bar = new Fl_Progress(MARGIN, y + (kStatusH - kGaugeH) / 2, kGaugeW, kGaugeH);
    m_bar->box(FL_BORDER_BOX);
    m_bar->minimum(0.0);
    m_bar->maximum(1.0);
    m_bar->value(0.0);
    m_bar->selection_color(tc.highlight2);
    m_bar->color(tc.background1);
    m_bar->label("");
    m_bar->hide();

    m_status = new Fl_Box(MARGIN, y, W - 2 * MARGIN, kStatusH);
    m_status->box(FL_NO_BOX);
    m_status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    m_status->labelsize(uiFontSize);
    m_status->labelcolor(tc.text1);
    y += kStatusH + 10;

    // --- source + re-check: control column left-aligned right after its label ---
    const int lblW = 64;
    Fl_Box *srcLbl = new Fl_Box(FL_NO_BOX, MARGIN, y, lblW, rowH,
                                I18n::get("update.page.source"));
    srcLbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    srcLbl->labelsize(uiFontSize);
    srcLbl->labelcolor(tc.text1);

    m_source = new SettingsChoice(MARGIN + lblW, y, 110, rowH);
    // Same construction as ParamDialog's --!param dropdowns (the format every
    // Lua script's parameter dialog uses): flat field background + the themed
    // 1px outline. Without this FLTK's scheme paints a raised grey button.
    m_source->box(FL_BORDER_BOX);
    m_source->down_box(FL_BORDER_BOX);
    m_source->textsize(uiFontSize);
    m_source->color(tc.background1);
    m_source->selection_color(tc.highlight2);
    m_source->textcolor(tc.text1);
    m_source->add("Gitee");
    m_source->add("GitHub");
    m_source->value(cfg ? cfg->getUpdateSource() : 0);
    m_source->callback(cbSource, this);

    m_btnRefresh = new HoverButton(0, y, 0, rowH, I18n::get("update.page.refresh"));
    m_btnRefresh->color(tc.background1);
    m_btnRefresh->selection_color(tc.highlight1);
    m_btnRefresh->setPressColor(tc.highlight2);
    m_btnRefresh->labelcolor(tc.text1);
    m_btnRefresh->labelsize(uiFontSize);
    m_btnRefresh->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    m_btnRefresh->fit();
    m_btnRefresh->position(MARGIN + lblW + 110 + GAP * 2, y);
    m_btnRefresh->callback(cbRefresh, this);
    y += rowH + 8;

    // --- versions ---
    const int half = (W - 2 * MARGIN) / 2;
    m_curVer = new Fl_Box(FL_NO_BOX, MARGIN, y, half, rowH, nullptr);
    m_curVer->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_curVer->labelsize(uiFontSize);
    m_curVer->labelcolor(tc.text1);
    m_mfVer = new Fl_Box(FL_NO_BOX, MARGIN + half, y, half, rowH, nullptr);
    m_mfVer->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_mfVer->labelsize(uiFontSize);
    m_mfVer->labelcolor(tc.text1);
    y += rowH + 8;

    // --- changelog (scrollable: a release note is not a one-line message) ---
    Fl_Box *notesLbl = new Fl_Box(FL_NO_BOX, MARGIN, y, W - 2 * MARGIN, uiFontSize + 4,
                                  I18n::get("update.page.notes"));
    notesLbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    notesLbl->labelsize(uiFontSize);
    notesLbl->labelcolor(tc.text1);
    y += uiFontSize + 4 + 4;

    m_notesBuf = new Fl_Text_Buffer();
    m_notes = new ThemedTextDisplay(MARGIN, y, W - 2 * MARGIN, NOTES_H, tc);
    m_notes->box(FL_BORDER_BOX);   // the themed 1px outline, like every input
    m_notes->buffer(m_notesBuf);   // takes its own reference on the buffer
    m_notes->textsize(uiFontSize);
    m_notes->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
    y += NOTES_H + 8;

    // --- footer: [更新][关闭]. The dismissive button stays rightmost, the
    //     same rule every other Pecia dialog follows.
    m_btnDownload = new HoverButton(0, 0, 0, rowH, I18n::get("update.page.download"));
    m_btnDownload->callback(cbDownload, this);
    m_btnClose = new HoverButton(0, 0, 0, rowH, I18n::get("update.page.close"));
    createDialogButtonBar(0, y, W, gBarH, tc, uiFontSize,
                          { m_btnDownload, m_btnClose }, MARGIN, GAP);
    m_btnClose->callback(cbClose, this);

    size(W, y + gBarH);
    end();
    finalizeShell();

    // Nothing is installable until a manifest says so. Availability is shown by
    // dimming the LABEL only (see setInstallable) - Fl_Widget::deactivate()
    // repaints the box with an opaque grey wash, and then the hover colour is
    // wrong too.
    setInstallable(false);
}

UpdateDialog::~UpdateDialog() {
    // Any worker still running sees this and drops its payload.
    *m_alive = false;
    if (m_notesBuf) {
        // Fl_Text_Display does NOT own the buffer but its destructor reaches
        // into whatever mBuffer still points at - so detach first, then delete.
        if (m_notes) m_notes->buffer(nullptr);
        delete m_notesBuf;
        m_notesBuf = nullptr;
    }
}

void UpdateDialog::run() {
    position((Fl::w() - w()) / 2, (Fl::h() - h()) / 2);
    show();
    // Deliberately no network here: a user who just wants to read the release
    // notes of the version they already installed gets them from the local
    // manifest, and the last check's answer stays on disk until they ask again.
    showCachedState();
    while (shown()) Fl::wait();
}

void UpdateDialog::cbClose(Fl_Widget *, void *data) {
    static_cast<UpdateDialog *>(data)->requestClose();
}

void UpdateDialog::requestClose() {
    *m_alive = false;      // drop any in-flight worker result
    hide();
}

// The status line is one short string out of a fixed set of five (checking /
// check ok / check failed / local manifest / update failed), always red so it is
// findable at a glance. Nothing else goes here: the version fields and whether
// the update button is dimmed carry the rest, and an empty string means "no news".
void UpdateDialog::setStatus(const char *text) {
    m_statusBuf = text ? text : "";
    m_status->label(m_statusBuf.c_str());
    m_status->labelcolor(dangerColour());
    m_status->redraw();
}

void UpdateDialog::setBusy(bool busy) {
    m_busy = busy;
    Fl_Color fg = normalColour(), dim = hintColour();
    // Labels only: the click guards in startCheck()/startDownload()/cbSource()
    // are what actually block a second concurrent worker.
    m_btnRefresh->labelcolor(busy ? dim : fg);
    m_source->textcolor(busy ? dim : fg);
    m_btnRefresh->redraw();
    m_source->redraw();
    if (busy) setInstallable(false);
    else      setInstallable(m_canInstall);
}

void UpdateDialog::showNotes(const std::string &text) {
    if (!m_notesBuf) return;
    m_notesBuf->text(text.empty() ? " " : text.c_str());
    m_notes->scroll(0, 0);
    m_notes->redraw();
}

// Show/hide the gauge and give the text back its full width. The text always
// starts at a fixed x in each mode, so a long translation never moves anything:
// idle it spans the row, transferring it begins right after the gauge.
void UpdateDialog::setTransferMode(bool on) {
    const int fullW = w() - 2 * MARGIN;
    const int textX = MARGIN + kGaugeW + GAP * 2;
    if (on) {
        m_bar->show();
        m_status->resize(textX, m_statusY, fullW - kGaugeW - GAP * 2, kStatusH);
    } else {
        m_bar->hide();
        m_bar->value(0.0);
        m_status->resize(MARGIN, m_statusY, fullW, kStatusH);
    }
    redraw();
}

void UpdateDialog::setProgress(double frac, const std::string &text) {
    if (frac < 0) frac = 0.0;
    m_bar->value(static_cast<float>(frac > 1.0 ? 1.0 : frac));
    m_bar->redraw();
    setStatus(text.c_str());
}

void UpdateDialog::finishFailure(const std::string &message) {
    setBusy(false);
    setTransferMode(false);     // nothing is transferring any more
    setStatus(message.c_str());
    setInstallable(false);
}

// ---------------------------------------------------------------------------
// Display helpers
// ---------------------------------------------------------------------------

Fl_Color UpdateDialog::normalColour() const {
    return theme() ? theme()->colors().text1 : FL_BLACK;
}

Fl_Color UpdateDialog::hintColour() const {
    return theme() ? theme()->colors().text2 : FL_GRAY;
}

Fl_Color UpdateDialog::dangerColour() const {
    return theme() ? theme()->colors().highlight3 : FL_RED;
}

// Availability is a LABEL colour, not a widget state: Fl_Widget::deactivate()
// repaints the box with an opaque grey wash, and then the hover fill is wrong
// too. The click guards in startCheck()/startDownload()/cbSource() are what
// actually block a second concurrent worker.
void UpdateDialog::setInstallable(bool can) {
    m_canInstall = can;
    m_btnDownload->labelcolor(can ? normalColour() : hintColour());
    m_btnDownload->redraw();
}

std::string UpdateDialog::downloadUrl() const {
    return (m_source->value() == 1) ? m_manifest.urlGithub : m_manifest.urlGitee;
}

// One printf-shaped helper, because the status line composes two of these.
static std::string fmt(const char *key, const char *a, const char *b) {
    char buf[512];
    snprintf(buf, sizeof(buf), I18n::get(key), a, b);
    return buf;
}

void UpdateDialog::renderManifest(const UpdateManifest &m) {
    m_curVerBuf = fmt("update.page.current", PECIA_VERSION, "");
    m_curVer->label(m_curVerBuf.c_str());
    m_mfVerBuf = m.version.empty()
                     ? std::string(I18n::get("update.page.manifest_none"))
                     : fmt("update.page.manifest", m.version.c_str(), "");
    m_mfVer->label(m_mfVerBuf.c_str());
    m_curVer->redraw();
    m_mfVer->redraw();
    // Release notes follow the UI language. Chinese is the source of truth, so
    // every language that has no translation yet reads the Chinese section
    // rather than nothing at all.
    const char *code = I18n::currentCode();
    const bool useEn = code && strncmp(code, "zh", 2) != 0 && !m.notesEn.empty();
    showNotes(useEn ? m.notesEn : m.notes);
}

// Installing needs three things: a version newer than ours, a URL for the
// source the user picked, and a hash to verify against. Without the hash we
// refuse rather than lay down an unverified package.
// A manifest we cannot act on (no hash, or no URL for the chosen source) is
// shown by keeping the update button dimmed - not by writing a sentence about it.
void UpdateDialog::applyInstallability(bool newer) {
    if (!newer) { setInstallable(false); return; }
    setInstallable(!m_manifest.sha256.empty() && !downloadUrl().empty());
}

// What the last successful check left on disk. Two words say where it came from,
// which is also the honesty guard: a cached answer is never reported as "latest".
void UpdateDialog::showCachedState() {
    UpdateManifest m;
    long long age = -1;
    if (!readCachedManifest(m, age)) {
        m_manifest = UpdateManifest();
        renderManifest(m_manifest);
        setStatus("");
        setInstallable(false);
        return;
    }
    m_manifest = m;
    renderManifest(m);
    setStatus(I18n::get("update.page.local"));
    applyInstallability(compareVersions(PECIA_VERSION, m.version) > 0);
}

// ---------------------------------------------------------------------------
// Check
// ---------------------------------------------------------------------------

void UpdateDialog::startCheck() {
    if (m_busy || !m_cfg) return;
    char gb[512] = { 0 }, hb[512] = { 0 };
    m_cfg->getUpdateUrlGitee(gb, sizeof(gb));
    m_cfg->getUpdateUrlGithub(hb, sizeof(hb));
    const std::string g = gb, h = hb;
    const int source = m_source->value();

    setBusy(true);
    setStatus(I18n::get("update.page.checking"));

    std::shared_ptr<bool> alive = m_alive;
    std::thread([this, alive, source, g, h]() {
        try {
            UpdateCheckResult r = checkUpdate(PECIA_VERSION, g, h, source);
            auto *job = new Job<UpdateCheckResult>{ alive, this, r };
            Fl::awake([](void *d) {
                auto *j = static_cast<Job<UpdateCheckResult> *>(d);
                if (*j->alive) j->self->applyCheckResult(j->payload);
                delete j;
            }, job);
        } catch (...) {
            auto *job = new Job<std::string>{ alive, this,
                                              I18n::get("update.page.failed") };
            Fl::awake([](void *d) {
                auto *j = static_cast<Job<std::string> *>(d);
                if (*j->alive) j->self->finishFailure(j->payload);
                delete j;
            }, job);
        }
    }).detach();
}

void UpdateDialog::applyCheckResult(const UpdateCheckResult &r) {
    setBusy(false);
    setTransferMode(false);     // a re-check ends any leftover transfer display
    m_manifest = r.manifest;
    renderManifest(m_manifest);

    if (!r.online && !r.fromCache) {
        setStatus(I18n::get("update.page.failed"));
        setInstallable(false);
        return;
    }
    // Whether there IS a newer version is readable in the 最新版 field and in the
    // update button; the line itself only says if the answer came from the
    // network or from the local manifest.
    setStatus(I18n::get(r.fromCache ? "update.page.local" : "update.page.ok"));
    applyInstallability(r.hasUpdate);
}

// ---------------------------------------------------------------------------
// Download + verify + hand off to the updater script
// ---------------------------------------------------------------------------

void UpdateDialog::startDownload() {
    if (m_busy || !m_cfg || !m_canInstall) return;
    const std::string url = downloadUrl();
    if (url.empty() || m_manifest.sha256.empty()) return;   // belt and braces

    const std::string exeDir = getExeDirUtf8();
    std::string ver = m_manifest.version;
    if (!ver.empty() && (ver[0] == 'v' || ver[0] == 'V')) ver.erase(0, 1);
    const std::string zipPath  = exeDir + "Pecia_x64_" + ver + ".zip";
    const std::string wantHash = m_manifest.sha256;

    setBusy(true);
    setTransferMode(true);
    setProgress(0.0, I18n::get("update.downloading"));

    std::shared_ptr<bool> alive = m_alive;
    std::thread([this, alive, url, zipPath, wantHash, exeDir]() {
        auto fail = [this, alive](const std::string &msg) {
            auto *job = new Job<std::string>{ alive, this, msg };
            Fl::awake([](void *d) {
                auto *j = static_cast<Job<std::string> *>(d);
                if (*j->alive) j->self->finishFailure(j->payload);
                delete j;
            }, job);
        };

        try {
            auto progress = [this, alive](long long received, long long total) {
                auto *job = new Job<std::pair<long long, long long>>{
                    alive, this, { received, total } };
                Fl::awake([](void *d) {
                    auto *j = static_cast<Job<std::pair<long long, long long>> *>(d);
                    if (*j->alive) {
                        const long long got = j->payload.first, all = j->payload.second;
                        char buf[96];
                        if (all > 0)
                            snprintf(buf, sizeof(buf), "%s  %.0f%%",
                                     I18n::get("update.downloading"), 100.0 * got / all);
                        else
                            snprintf(buf, sizeof(buf), "%s  %lld KB",
                                     I18n::get("update.downloading"), got / 1024);
                        j->self->setProgress(all > 0 ? (double)got / (double)all : -1.0, buf);
                    }
                    delete j;
                }, job);
            };

            if (!downloadFile(url, zipPath, progress)) {
                fail(I18n::get("update.page.updatefailed"));
                return;
            }

            std::string got;
            if (!sha256FileUtf8(zipPath, got) || got != wantHash) {
                // Mismatch = not the package the manifest describes (truncated,
                // swapped in transit, or the two mirrors differ). Delete it: a
                // retry must not be able to pick up the bad file.
                deleteFileUtf8(zipPath);
                fail(I18n::get("update.page.updatefailed"));
                return;
            }

            const std::string batPath = exeDir + "pecia_update.bat";
            if (!writeUpdateBat(exeDir, zipPath, batPath)) {
                fail(I18n::get("update.page.updatefailed"));
                return;
            }

            auto *job = new Job<std::string>{ alive, this, batPath };
            Fl::awake([](void *d) {
                auto *j = static_cast<Job<std::string> *>(d);
                UpdateDialog *self = j->self;
                std::string bat = j->payload;
                const bool stillThere = *j->alive;
                delete j;
                if (!stillThere) return;   // user closed the window mid-download
                // Handing off kills this process, so never lose work silently.
                if (self->m_confirmUnsaved && !self->m_confirmUnsaved()) {
                    self->setBusy(false);
                    self->applyInstallability(true);
                    return;
                }
                self->requestClose();      // stop every further worker callback
                launchUpdater(bat);
                std::exit(0);
            }, job);
        } catch (...) {
            fail(I18n::get("update.page.exception"));
        }
    }).detach();
}

void UpdateDialog::cbRefresh(Fl_Widget *, void *data) {
    static_cast<UpdateDialog *>(data)->startCheck();
}

void UpdateDialog::cbDownload(Fl_Widget *, void *data) {
    static_cast<UpdateDialog *>(data)->startDownload();
}

void UpdateDialog::cbSource(Fl_Widget *, void *data) {
    auto *self = static_cast<UpdateDialog *>(data);
    if (self->m_busy || !self->m_cfg) return;
    self->m_cfg->setUpdateSource(self->m_source->value());
    // The source only decides WHICH URL a package would come from, so switching
    // it re-evaluates availability - it does not start a download or a check.
    if (self->m_manifest.version.empty()) return;
    self->applyInstallability(compareVersions(PECIA_VERSION,
                                              self->m_manifest.version) > 0);
}
