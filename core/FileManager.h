// FileManager.h - File open/save/save-as operations
#pragma once

#include <functional>
#include <string>

class Document;
class Config;
class Fl_Window;

// FileManager
//   Handles file open, save, save-as, and associated dialogs.
//   Communicates with the MainWindow through callbacks for UI
//   operations (tab switching, status bar updates, etc.).
class FileManager {
public:
    using SimpleCallback = std::function<void()>;
    using TabCallback = std::function<void(int)>;

    FileManager(Config *cfg);

    // Check if the document has unsaved changes. If so, prompt the user.
    // Returns true if safe to close (saved, discarded, or not dirty).
    // Calls the provided switchTab callback to show the tab before prompting.
    bool checkSaveBeforeClose(Document *doc, int tabIndex,
                              TabCallback switchTab,
                              SimpleCallback updateLabel,
                              SimpleCallback updateTitle,
                              SimpleCallback updateStatus);

    // Save the document. If it has no path, delegates to saveAs.
    bool save(Document *doc, int tabIndex,
              SimpleCallback updateLabel,
              SimpleCallback updateTitle,
              SimpleCallback updateStatus);

    // Save As: show a file dialog and save to the chosen path.
    bool saveAs(Document *doc, Config *cfg, Fl_Window *parent);

private:
    Config *m_cfg;

    // Internal helper: show the native Save As dialog.
    // Returns the chosen path (empty if cancelled).
    std::string showSaveDialog(Config *cfg, Fl_Window *parent);
};
