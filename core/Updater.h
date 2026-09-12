// Updater.h - self-update support for Pecia (Windows x64).
//
// Flow (mirrors the CurSeen reference and the friend's "independent updater
// process" design):
//   1. checkForUpdate()  -> query Gitee Releases API, compare version.
//   2. downloadFile()    -> download the new zip next to the running exe.
//   3. writeUpdateBat()  -> emit pecia_update.bat that replaces files + restarts.
//   4. launchUpdater()   -> start that bat (hidden), caller then exits.
//
// All path APIs are wide-char / UTF-8 aware so Chinese paths work end to end
// (executable path, download path, extract path, restart path).
//
// This module is intentionally FLTK-free so it can run on a worker thread.

#pragma once

#include <functional>
#include <string>

struct UpdateInfo {
    bool        hasUpdate = false;   // server version is newer than current
    std::string latest;             // tag_name, e.g. "v1.0.1"
    std::string notes;              // release body (changelog)
    std::string url;                // constructed download URL
    std::string error;             // non-empty on transport / parse failure
};

// Query Gitee for the latest Pecia release and compare with `currentVersion`
// (e.g. "1.0.0"). Synchronous and blocking — call from a worker thread.
UpdateInfo checkForUpdate(const std::string &currentVersion);

// Progress report for downloadFile: (bytesReceived, totalBytes).
// `total` is 0 when the server did not send a Content-Length, so callers
// must treat 0 as "unknown" and show an indeterminate indicator.
// Invoked on the downloading (worker) thread - marshal before touching UI.
using DownloadProgress = std::function<void(long long received, long long total)>;

// Download `url` to `savePathUtf8` (UTF-8, may contain Chinese). Uses wide
// file APIs so Chinese paths work. Returns true on success.
// `progress` is optional and may be an empty std::function.
bool downloadFile(const std::string &url, const std::string &savePathUtf8,
                  DownloadProgress progress = nullptr);

// Write pecia_update.bat into exeDir. When run (after the app exits) it
// kills Pecia, extracts the zip, robocopies over the install, restarts, and
// deletes itself. Paths may contain Chinese (UTF-8 BOM + chcp 65001).
bool writeUpdateBat(const std::string &exeDirUtf8,
                    const std::string &zipPathUtf8,
                    const std::string &batPathUtf8);

// Launch the updater bat (hidden window). The caller should exit the process
// immediately afterwards (e.g. Fl::exit(0)) so the bat can replace the exe.
void launchUpdater(const std::string &batPathUtf8);

// Directory of the running executable (UTF-8, with trailing backslash).
// Wide-char safe for Chinese paths.
std::string getExeDirUtf8();
