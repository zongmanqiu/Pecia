// ShortcutManager.h - Keyboard shortcut dispatch
#pragma once

#include <FL/Fl.H>
#include <functional>
#include <vector>

// ShortcutManager
//   Centralizes keyboard shortcut dispatch. Maps key combinations
//   to actions, keeping the dispatch logic out of MainWindow::handle().
class ShortcutManager {
public:
    using Action = std::function<bool()>;

    // Register a shortcut handler. The action should return true if handled.
    void add(int key, unsigned requiredMods, Action action);

    // Convenience: no modifier (just the key).
    void add(int key, Action action) { add(key, 0, std::move(action)); }

    // Dispatch a keyboard event. Returns true if handled.
    bool dispatch(int key, unsigned state) const;

    // Remove all registered shortcuts.
    void clear();

private:
    struct Entry {
        int      key;
        unsigned mods;
        Action   action;
    };
    std::vector<Entry> m_entries;
};
