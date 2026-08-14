// FileManager.cpp - File open/save/save-as implementation
#include "FileManager.h"
#include "Document.h"
#include "core/Config.h"
#include "core/I18n.h"

#include <FL/Fl.H>
#include "core/UiBridge.h"
#include <FL/Fl_File_Chooser.H>
#include <FL/filename.H>

#include <string.h>

FileManager::FileManager(Config *cfg)
    : m_cfg(cfg) {
}

bool FileManager::checkSaveBeforeClose(Document *doc, int tabIndex,
                                       TabCallback switchTab,
                                       SimpleCallback updateLabel,
                                       SimpleCallback updateTitle,
                                       SimpleCallback updateStatus) {
    if (!doc || !doc->isDirty()) return true;

    // Switch to the tab so the user sees what they're being asked about
    if (switchTab) switchTab(tabIndex);

    UiBridge *bridge = uiBridge();
    ConfirmChoice c = bridge ? bridge->confirm(I18n::get("confirm.unsaved.title"),
        I18n::get("confirm.unsaved.msg"),
        I18n::get("dlg.save"), I18n::get("dlg.dontsave"), I18n::get("dlg.cancel"))
        : ConfirmChoice::Second;
    if (c == ConfirmChoice::First) return save(doc, tabIndex, updateLabel, updateTitle, updateStatus);
    if (c == ConfirmChoice::Second) return true;
    return false;
}

bool FileManager::save(Document *doc, int /*tabIndex*/,
                       SimpleCallback updateLabel,
                       SimpleCallback updateTitle,
                       SimpleCallback updateStatus) {
    if (!doc) return false;

    if (doc->filePath()[0]) {
        bool ok = doc->saveFile();
        if (ok) {
            if (updateLabel) updateLabel();
            if (updateTitle) updateTitle();
            if (updateStatus) updateStatus();
        }
        return ok;
    }
    return saveAs(doc, m_cfg, nullptr);
}

bool FileManager::saveAs(Document *doc, Config *cfg, Fl_Window *parent) {
    if (!doc) return false;
    (void)parent;

    std::string path = showSaveDialog(cfg, nullptr);
    if (path.empty()) return false;

    bool ok = doc->saveFile(path.c_str());
    if (ok && cfg) {
        cfg->recentAdd(path.c_str());
    }
    return ok;
}

std::string FileManager::showSaveDialog(Config *cfg, Fl_Window * /*parent*/) {
    // Use FLTK's native file chooser
    const char *filter = "Text Files (*.txt)\t*.txt\n"
                         "All Files (*)\t*";

    char initialDir[FL_PATH_MAX] = "";
    if (cfg) {
        cfg->getLastDir(initialDir, sizeof(initialDir));
    }

    const char *result = fl_file_chooser("Save As...", filter, initialDir);
    if (!result) return {};

    // Save the chosen directory for next time
    if (cfg && result[0]) {
        char dir[FL_PATH_MAX];
        strncpy(dir, result, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = '\0';
        char *p = strrchr(dir, '/');
        if (!p) p = strrchr(dir, '\\');
        if (p) *p = '\0';
        cfg->setLastDir(dir);
    }

    return std::string(result);
}
