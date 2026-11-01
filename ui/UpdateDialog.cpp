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
#include <cstdlib>
#include <ctime>
#include <thread>
#include <utility>

namespace {

const int MARGIN   = 16;
const int GAP      = 8;
const int NOTES_H  = 170;
const int kStatusH = 40;   // two wrapped lines

// "2026-10-30 14:22" derived from the cache file's age, so a cached verdict
// states how old it is instead of pretending to be current.
std::string ageToString(long long ageSeconds) {
    if (ageSeconds < 0) return std::string("(?)");
    std::time_t t = std::time(nullptr) - static_cast<std::time_t>(ageSeconds);
    std::tm tmv{};
    if (localtime_s(&tmv, &t) != 0) return std::string("(?)");
    char buf[32];
    if (!std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv)) return std::string("(?)");
    return buf;
}

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
    m_source->add("Gitee");
    m_source->add("GitHub");
    m_source->value(cfg ? cfg->getUpdateSource() : 0);
    m_source->callback(cbSource, this);

    m_btnRefresh = new HoverButton(0, y, 0, rowH, I18n::get("update.page.refresh"));
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
    m_curVer->labelcolor(tc.text2);
    m_mfVer = new Fl_Box(FL_NO_BOX, MARGIN + half, y, half, rowH, nullptr);
    m_mfVer->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_mfVer->labelsize(uiFontSize);
    m_mfVer->labelcolor(tc.text2);
    y += rowH + 8;

    // --- changelog (scrollable: a release note is not a one-line message) ---
    Fl_Box *notesLbl = new Fl_Box(FL_NO_BOX, MARGIN, y, W - 2 * MARGIN, uiFontSize + 4,
                                  I18n::get("update.page.notes"));
    notesLbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    notesLbl->labelsize(uiFontSize);
    notesLbl->labelcolor(tc.text2);
    y += uiFontSize + 4 + 4;

    m_notesBuf = new Fl_Text_Buffer();
    m_notes = new ThemedTextDisplay(MARGIN, y, W - 2 * MARGIN, NOTES_H, tc);
    m_notes->box(FL_BORDER_BOX);   // the themed 1px outline, like every input
    m_notes->buffer(m_notesBuf);   // takes its own reference on the buffer
    m_notes->textsize(uiFontSize);
    m_notes->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
    y += NOTES_H + 8;

    // --- progress row, hidden until a download starts (absolute coords: FLTK
    //     children are positioned in window space, joining a group does not offset) ---
    m_progRow = new Fl_Group(MARGIN, y, W - 2 * MARGIN, rowH);
    m_progRow->box(FL_NO_BOX);
    m_progRow->begin();
    m_progText = new Fl_Box(MARGIN, y, 140, rowH, nullptr);
    m_progText->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_progText->labelsize(uiFontSize);
    m_progText->labelcolor(tc.text2);
    m_bar = new Fl_Progress(MARGIN + 148, y + 2, W - 2 * MARGIN - 148, rowH - 4);
    m_bar->minimum(0.0);
    m_bar->maximum(1.0);
    m_bar->value(0.0);
    m_bar->selection_color(tc.highlight2);
    m_bar->color(tc.background3);
    m_bar->labelsize(uiFontSize - 2);
    m_progRow->end();
    m_progRow->hide();
    y += rowH + 8;

    // --- footer: [下载并更新][关闭]. The dismissive button stays rightmost,
    //     the same rule every other Pecia dialog follows.
    m_btnDownload = new HoverButton(0, 0, 0, rowH, I18n::get("update.page.download"));
    m_btnDownload->callback(cbDownload, this);
    m_btnClose = new HoverButton(0, 0, 0, rowH, I18n::get("update.page.close"));
    createDialogButtonBar(0, y, W, gBarH, tc, uiFontSize,
                          { m_btnDownload, m_btnClose }, MARGIN, GAP);
    m_btnClose->callback(cbClose, this);

    size(W, y + gBarH);
    end();
    finalizeShell();

    setProgress(0.0, std::string());
    m_btnDownload->deactivate();
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
    // Started only once the window is on screen: the first frame shows the
    // cached manifest (if any), then the network verdict replaces it.
    startCheck();
    while (shown()) Fl::wait();
}

void UpdateDialog::cbClose(Fl_Widget *, void *data) {
    static_cast<UpdateDialog *>(data)->requestClose();
}

void UpdateDialog::requestClose() {
    *m_alive = false;      // drop any in-flight worker result
    hide();
}

void UpdateDialog::setStatus(const char *text, Fl_Color colour) {
    m_statusBuf = text ? text : "";
    m_status->label(m_statusBuf.c_str());
    m_status->labelcolor(colour);
    m_status->redraw();
}

void UpdateDialog::setBusy(bool busy) {
    m_busy = busy;
    if (busy) {
        m_btnRefresh->deactivate();
        m_btnDownload->deactivate();
        m_source->deactivate();
    } else {
        m_btnRefresh->activate();
        m_source->activate();
        // The download button is re-enabled by applyCheckResult(), which owns
        // the "is there something to install" decision.
    }
    m_btnRefresh->redraw();
    m_btnDownload->redraw();
    m_source->redraw();
}

void UpdateDialog::showNotes(const std::string &text) {
    if (!m_notesBuf) return;
    m_notesBuf->text(text.empty() ? " " : text.c_str());
    m_notes->scroll(0, 0);
    m_notes->redraw();
}

void UpdateDialog::setProgress(double frac, const std::string &text) {
    m_progTextBuf = text;
    m_progText->label(m_progTextBuf.c_str());
    m_bar->value(static_cast<float>(frac < 0 ? 0.0 : (frac > 1.0 ? 1.0 : frac)));
    m_progRow->redraw();
}

void UpdateDialog::finishFailure(const std::string &message) {
    setBusy(false);
    m_progRow->hide();
    m_progRow->redraw();
    setStatus(message.c_str(),
              theme() ? theme()->colors().highlight3 : FL_RED);
    m_btnDownload->deactivate();
    m_btnDownload->redraw();
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
    setStatus(I18n::get("update.page.checking"), hintColour());

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
                                              I18n::get("update.page.exception") };
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
    const Fl_Color normal = theme() ? theme()->colors().text1 : FL_BLACK;
    const Fl_Color hint   = theme() ? theme()->colors().text2 : FL_GRAY;
    const Fl_Color danger = theme() ? theme()->colors().highlight3 : FL_RED;
    char line[640];

    // The two version fields always describe what is on screen, network or not.
    snprintf(line, sizeof(line), I18n::get("update.page.current"), PECIA_VERSION);
    m_curVerBuf = line;
    m_curVer->label(m_curVerBuf.c_str());
    if (r.manifest.version.empty()) {
        m_mfVerBuf = I18n::get("update.page.manifest_none");
    m_mfVer->label(m_mfVerBuf.c_str());
    } else {
        snprintf(line, sizeof(line), I18n::get("update.page.manifest"),
                 r.manifest.version.c_str());
        m_mfVerBuf = line;
        m_mfVer->label(m_mfVerBuf.c_str());
    }
    m_curVer->redraw();
    m_mfVer->redraw();

    showNotes(r.manifest.notes);

    if (!r.online && !r.fromCache) {
        snprintf(line, sizeof(line), I18n::get("update.error"), r.error.c_str());
        setStatus(line, danger);
        m_btnDownload->deactivate();
        m_btnDownload->redraw();
        return;
    }

    m_manifest = r.manifest;

    if (r.hasUpdate) {
        snprintf(line, sizeof(line), I18n::get("update.newAvailable"),
                 r.manifest.version.c_str());
        std::string s = line;
        if (r.fromCache) {
            char sub[320];
            snprintf(sub, sizeof(sub), I18n::get("update.page.cacheoffline"),
                     ageToString(r.cacheAgeSec).c_str());
            s += sub;
        }
        setStatus(s.c_str(), normal);
    } else if (r.online) {
        // Only a fresh answer may claim "you are up to date".
        setStatus(I18n::get("update.latest"), normal);
    } else {
        // A cache that is not newer than the running build proves nothing:
        // it was written by whichever build last checked, possibly this one's
        // predecessor. Say "unverified", never "latest".
        char sub[320];
        snprintf(sub, sizeof(sub), I18n::get("update.page.cacheoffline"),
                 ageToString(r.cacheAgeSec).c_str());
        setStatus(sub, hint);
    }

    // Installing needs a URL for the selected source AND a hash to verify
    // against: without the hash we refuse rather than lay down an unverified包.
    const int source = m_source->value();
    const std::string url = (source == 1) ? m_manifest.urlGithub : m_manifest.urlGitee;
    bool canInstall = r.hasUpdate && !url.empty();
    if (canInstall && m_manifest.sha256.empty()) {
        setStatus(I18n::get("update.page.nohash"), danger);
        canInstall = false;
    } else if (canInstall) {
        m_btnDownload->activate();
    } else {
        m_btnDownload->deactivate();
    }
    m_btnDownload->redraw();
}

// ---------------------------------------------------------------------------
// Download + verify + hand off to the updater script
// ---------------------------------------------------------------------------

void UpdateDialog::startDownload() {
    if (m_busy || !m_cfg) return;
    const int source = m_source->value();
    const std::string url = (source == 1) ? m_manifest.urlGithub : m_manifest.urlGitee;
    if (url.empty() || m_manifest.sha256.empty()) return;

    const std::string exeDir = getExeDirUtf8();
    std::string ver = m_manifest.version;
    if (!ver.empty() && (ver[0] == 'v' || ver[0] == 'V')) ver.erase(0, 1);
    const std::string zipPath  = exeDir + "Pecia_x64_" + ver + ".zip";
    const std::string wantHash = m_manifest.sha256;

    setBusy(true);
    m_progRow->show();
    setProgress(0.0, I18n::get("update.downloading"));

    std::shared_ptr<bool> alive = m_alive;
    std::thread([this, alive, url, zipPath, wantHash, exeDir]() {
        // Failure hop: red status, buttons back to normal.
        auto fail = [this, alive](const std::string &msg) {
            auto *job = new Job<std::string>{ alive, this, msg };
            Fl::awake([](void *d) {
                auto *j = static_cast<Job<std::string> *>(d);
                if (*j->alive) j->self->finishFailure(j->payload);
                delete j;
            }, job);
        };
        // Stage hop: keep the window busy and just rewrite the status line.
        auto stage = [this, alive](const std::string &msg) {
            auto *job = new Job<std::string>{ alive, this, msg };
            Fl::awake([](void *d) {
                auto *j = static_cast<Job<std::string> *>(d);
                if (*j->alive)
                    j->self->setStatus(j->payload.c_str(),
                                       j->self->hintColour());
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
                        char buf[64];
                        if (all > 0) snprintf(buf, sizeof(buf), "%.0f%%", 100.0 * got / all);
                        else         snprintf(buf, sizeof(buf), "%lld KB", got / 1024);
                        j->self->setProgress(all > 0 ? (double)got / (double)all : -1.0, buf);
                    }
                    delete j;
                }, job);
            };

            if (!downloadFile(url, zipPath, progress)) {
                fail(I18n::get("update.dlfail"));
                return;
            }

            stage(I18n::get("update.page.verifying"));

            std::string got;
            if (!sha256FileUtf8(zipPath, got) || got != wantHash) {
                // Mismatch = not the package the manifest describes (truncated,
                // swapped in transit, or the two mirrors differ). Delete it: a
                // retry must not be able to pick up the bad file.
                deleteFileUtf8(zipPath);
                char buf[512];
                snprintf(buf, sizeof(buf), I18n::get("update.page.hashfail"),
                         wantHash.c_str(), got.c_str());
                fail(buf);
                return;
            }

            const std::string batPath = exeDir + "pecia_update.bat";
            if (!writeUpdateBat(exeDir, zipPath, batPath)) {
                fail(I18n::get("update.batfail"));
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
                    self->setStatus(I18n::get("update.page.cancelled"), self->hintColour());
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

Fl_Color UpdateDialog::hintColour() const {
    return theme() ? theme()->colors().text2 : FL_GRAY;
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
    // Switching source is a request for a different answer: re-check now.
    self->startCheck();
}
