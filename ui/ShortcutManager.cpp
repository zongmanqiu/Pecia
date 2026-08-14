// ShortcutManager.cpp - Shortcut dispatch implementation
#include "ShortcutManager.h"

void ShortcutManager::add(int key, unsigned requiredMods, Action action) {
    Entry e;
    e.key = key;
    e.mods = requiredMods;
    e.action = std::move(action);
    m_entries.push_back(std::move(e));
}

bool ShortcutManager::dispatch(int key, unsigned state) const {
    unsigned cmd = state & (FL_COMMAND | FL_SHIFT | FL_ALT | FL_META);
    for (const auto &e : m_entries) {
        if (e.key == key && e.mods == cmd) {
            if (e.action) return e.action();
        }
    }
    return false;
}

void ShortcutManager::clear() {
    m_entries.clear();
}
