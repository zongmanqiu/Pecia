// Updater.h - self-update support for Pecia (Windows x64).
//
// Flow (mirrors the CurSeen reference and the friend's "independent updater
// process" design):
//   1. checkUpdate()     -> fetch the update manifest (JSON) from the selected
//                           source, fall back to the other source, then to the
//                           local cache; compare versions.
//   2. downloadFile()    -> download the new zip next to the running exe.
//   3. sha256FileUtf8()  -> verify the package against the manifest hash.
//   4. writeUpdateBat()  -> emit pecia_update.bat that replaces files + restarts.
//   5. launchUpdater()   -> start that bat (hidden), caller then exits.
//
// Why a static manifest instead of the Gitee Releases API: a source switch then
// costs one URL, not a second API dialect (GitHub's releases endpoint has
// different field names and a 60 req/h unauthenticated quota), and the manifest
// can carry the sha256 the API cannot.
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
    std::string notes;        // changelog text
    std::string sha256;       // lowercase hex of the zip
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

// Reads the cached manifest next to the exe. Returns false when the file is
// absent or unparsable; `ageSeconds` is the file's age at call time.
bool readCachedManifest(UpdateManifest &out, long long &ageSeconds);

// Absolute path of the cache file (exe dir + update.cache.json). The name is
// deliberately NOT "update.json": the published manifest must never be shipped
// inside the zip, or robocopy would overwrite the live cache with the build-time
// copy on every update.
std::string updateCachePathUtf8();

// Lowercase hex SHA-256 of a file (UTF-8 path, may contain Chinese). Uses
// bcrypt.dll loaded with LOAD_LIBRARY_SEARCH_SYSTEM32 so this single call is
// what pins the DLL search -- no process-wide SetDefaultDllDirectories needed.
bool sha256FileUtf8(const std::string &pathUtf8, std::string &hexLower);

// Delete a file by UTF-8 path (wide API, so a Chinese install dir survives the
// round trip). Used to throw away a package that failed its hash check, so a
// retry can never pick up the bad file. A file that is already gone counts.
bool deleteFileUtf8(const std::string &pathUtf8);

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
