// Updater.h - self-update support for Pecia (Windows x64).
//
// Flow (mirrors the CurSeen reference and the friend's "independent updater
// process" design):
//   1. checkUpdate()     -> fetch the update manifest (JSON) from the selected
//                           source, fall back to the other source, then to the
//                           local cache; compare versions.
//   2. downloadFile()    -> download the new zip next to the running exe.
//   3. writeUpdateBat()  -> emit pecia_update.bat that replaces files + restarts.
//   4. launchUpdater()   -> start that bat (hidden), caller then exits.
//
// No package hash. The manifest and the package are served from the same
// repository, so a hash there cannot stop anyone who could swap the package
// anyway; it only costs the packer a step. Measured 2026-11-02: a truncated zip
// already fails in Expand-Archive (0 files land, and the bat's "Pecia.exe is
// missing" check catches it), while a zip with one flipped byte extracts
// "successfully" - that case is knowingly accepted.
//
// Why a static manifest instead of the Gitee Releases API: a source switch then
// costs one URL, not a second API dialect (GitHub's releases endpoint has
// different field names and a 60 req/h unauthenticated quota), and the manifest
// can carry the release notes in more than one language.
//
// All path APIs are wide-char / UTF-8 aware so Chinese paths work end to end
// (executable path, download path, extract path, restart path).
//
// This module is intentionally FLTK-free so it can run on a worker thread.

#pragma once

#include <functional>
#include <string>

// The published manifest (main/update.json, served raw by both mirrors).
// Flat keys on purpose: the JSON reader below is a flat "key":"value" scanner,
// and one nesting level would mean writing an object parser for a single file.
struct UpdateManifest {
    std::string version;      // "1.1.3"
    std::string published;    // "2026-11-05", display only
    std::string notes;        // changelog text, Chinese (HISTORY.md is the source)
    std::string notesEn;      // docs/history/en.md translation; empty = fall back
    std::string urlGitee;     // full download URL (not constructed here)
    std::string urlGithub;
};

// Outcome of one check attempt.
// `online` is the honesty bit: only a freshly fetched manifest may be reported
// as "you are up to date". A cache left over from an older build reads as
// "not checked" -- claiming "latest 1.1.2" while 1.1.3 exists is a lie we have
// seen a real product tell (CurSeen state.rs initial_status, 2026-10-31).
struct UpdateCheckResult {
    bool           online     = false;  // a manifest was fetched this call
    bool           fromCache  = false;  // verdict came from update.cache.json
    bool           hasUpdate  = false;  // manifest.version > currentVersion
    int            sourceUsed = 0;      // 0 = Gitee, 1 = GitHub
    long long      cacheAgeSec = -1;    // only with fromCache
    UpdateManifest manifest;
    std::string    error;               // set when neither source nor cache worked
};

// Tries `source` first, then the other non-empty URL, then the local cache.
// A successful fetch is written to the cache. Synchronous and blocking --
// call from a worker thread.
UpdateCheckResult checkUpdate(const std::string &currentVersion,
                              const std::string &urlGitee,
                              const std::string &urlGithub, int source);

// Compare two "x.y.z" versions (a leading "v" is tolerated): 1 when `latest` is
// newer than `cur`, -1 when older, 0 when equal. Public because the update page
// has to judge a cached manifest exactly the way checkUpdate judges a fresh one.
int compareVersions(const std::string &cur, const std::string &latest);

// Reads the cached manifest next to the exe. Returns false when the file is
// absent or unparsable; `ageSeconds` is the file's age at call time.
bool readCachedManifest(UpdateManifest &out, long long &ageSeconds);

// Absolute path of the cache file (exe dir + update.cache.json). The name is
// deliberately NOT "update.json": the published manifest must never be shipped
// inside the zip, or robocopy would overwrite the live cache with the build-time
// copy on every update.
std::string updateCachePathUtf8();

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
