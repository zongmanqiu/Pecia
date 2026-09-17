// MainWindow_timers.cpp - MainWindow timer-driven behavior: status bar
// refresh, cursor blink, auto-save. Each callback is armed via
// Fl::add_timeout and re-arms itself (or is armed by scheduleAutoSave).
// Grouped here per the MVC-style guidance: timers are a distinct module
// managed by the window. Moved from MainWindow.cpp; member functions are
// part of the same MainWindow class declared in ui/MainWindow.h.
#include "ui/MainWindow.h"

#include "editor/Document.h"
#include "editor/Editor.h"
#include "core/Config.h"
#include "core/I18n.h"
#include "ui/Layout.h"
#include <FL/Fl.H>
#include <stdio.h>
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

// --------------------------------------------------------------------------
// 对话框内存整理：打开/保存/另存为对话框关闭后 2s 主动 EmptyWorkingSet
// （工作集交还系统，任务管理器运行内存回落到基线）。
// 每次对话框关闭后都会整理（对话框操作天然低频，无需冷却；
// 第二次打开对话框时 shell 页面本就要重新加载）。
// --------------------------------------------------------------------------
void MainWindow::scheduleTrimSoon()
{
    Fl::remove_timeout(trimSoonCb, this);
    Fl::add_timeout(2.0, trimSoonCb, this);
}

void MainWindow::trimSoonCb(void *data)
{
    auto *win = static_cast<MainWindow *>(data);
    if (!win) return;
    // Skip the trim when the window holds a large document: EmptyWorkingSet
    // evicts the whole text buffer from the working set, and the NEXT
    // activation has to soft-fault every page back in - with a 50 MB file
    // that is exactly the "switching between document windows feels
    // sluggish" symptom (editing stays smooth, focus switch does not).
    {
        long long total = 0;
        for (auto &t : win->m_tabsList)
            if (t.doc && t.doc->buffer())
                total += t.doc->buffer()->length();
        if (total > kTrimSkipBytes) return;
    }
    EmptyWorkingSet(GetCurrentProcess());
}

// One-shot timeout callback - refreshes the status bar once and is done.
// Using add_timeout(0.0, ...) instead of add_idle() means we never have
// to call remove_idle() (and getting that match wrong was the source of
// a real CPU-leak bug: idle handlers piled up on every key press).
void MainWindow::statusUpdateCb(void *data) {
    auto *self = static_cast<MainWindow *>(data);
    if (!self) return;
    self->updateStatusBar();
    // If line highlight is on, update it when cursor moves to a new line.
    // updateLineHighlight() is a no-op if the line hasn't changed.
    if (self->m_cfg->getHighlightCurrentLine()) {
        Tab *t = self->activeTab();
        if (t) t->editor->updateLineHighlight();
    }
}

// Cursor blink timer callback. Toggles the active editor's cursor
// visibility and re-arms the timer for the next tick. We only touch
// the currently focused editor so hidden tabs don't get redrawn.
//
// Notes:
//  - Fl_Text_Display::show_cursor(int) sets the internal mCursorOn
//    flag; the actual drawing happens on the next redraw.
//  - We skip the toggle when the window has no focus or the editor
//    doesn't have focus - in those cases the cursor should stay off
//    anyway, and we avoid pointless redraws.
//  - The timer keeps running for the window's lifetime; at ~500ms with
//    a tiny callback the cost is negligible.
void MainWindow::cursorBlinkCb(void *data) {
    auto *self = static_cast<MainWindow *>(data);
    if (!self) return;
    self->m_cursorVisible = !self->m_cursorVisible;
    Tab *t = self->activeTab();
    if (t && Fl::focus() == t->editor && self->shown()) {
        t->editor->show_cursor(self->m_cursorVisible ? 1 : 0);
        t->editor->damage(FL_DAMAGE_CHILD);
    }
    Fl::add_timeout(0.5, cursorBlinkCb, self);
}

// --------------------------------------------------------------------------
// Auto-save: silently save all dirty documents that have a file path.
// --------------------------------------------------------------------------
void MainWindow::autoSaveCb(void *data) {
    auto *self = static_cast<MainWindow *>(data);
    if (!self) return;

    int interval = self->m_cfg->getAutoSaveInterval();
    if (interval <= 0) return;   // auto-save disabled

    // Save every dirty tab that already has a file path.
    // Unnamed files are skipped (no prompt).
    for (int i = 0; i < (int)self->m_tabsList.size(); ++i) {
        Document *doc = self->m_tabsList[i].doc;
        if (!doc->isDirty()) continue;
        if (!doc->filePath()[0]) continue;   // never auto-save unnamed
        doc->saveFile();
        // Silently update UI (no error alert on failure — auto-save
        // should be unobtrusive).
        self->updateTabLabel(i);
        self->updateTitle();
        self->updateStatusBar();
    }

    // Re-arm for the next interval.
    Fl::add_timeout((double)interval, autoSaveCb, self);
}

void MainWindow::scheduleAutoSave() {
    // Remove any pending auto-save timer.
    Fl::remove_timeout(autoSaveCb, this);
    int interval = m_cfg->getAutoSaveInterval();
    if (interval > 0) {
        Fl::add_timeout((double)interval, autoSaveCb, this);
    }
}

// --------------------------------------------------------------------------
// UI construction helpers
// --------------------------------------------------------------------------

