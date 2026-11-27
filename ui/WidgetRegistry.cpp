// WidgetRegistry.cpp - see ui/WidgetRegistry.h
#include "ui/WidgetRegistry.h"

#include <FL/Fl.H>
#include <FL/Fl_Widget.H>
#include <FL/Fl_Window.H>

#include <utility>

namespace {

// Death sentinel: FLTK deletes a widget's user_data in ~Fl_Widget when
// AUTO_DELETE_USER_DATA is set (src/Fl_Widget.cxx:183-184) and
// Fl_Callback_User_Data has a virtual destructor (FL/Fl_Widget.H:96). So
// this object is destroyed at the exact moment the window dies - no
// polling, and no way to be fooled by a dangling Fl_Window*.
class DeathSentinel : public Fl_Callback_User_Data {
public:
    explicit DeathSentinel(WidgetRegistry::LivenessPtr liveness)
        : m_liveness(std::move(liveness)) {}
    ~DeathSentinel() override {
        if (!m_liveness) return;
        m_liveness->markDead();
        // The hook fires here, even when the registration was withdrawn
        // before the window died - the registrant still has to know that
        // its widget pointer went stale.
        m_liveness->notifyDestroyed();
    }
private:
    WidgetRegistry::LivenessPtr m_liveness;
};

}  // namespace

WidgetRegistry &WidgetRegistry::instance()
{
    // Leaked on purpose: a window may be destroyed during static
    // destruction, and its death sentinel then calls back into the
    // registry - so the registry has to outlive every widget it tracks.
    static WidgetRegistry *r = new WidgetRegistry();
    return *r;
}

void WidgetRegistry::Liveness::notifyDestroyed()
{
    if (m_notified) return;
    m_notified = true;
    if (m_cb) m_cb(m_data);
}

int WidgetRegistry::add(Fl_Window *win, const char *kind,
                        DestroyedCallback onDestroyed, void *onDestroyedData)
{
    const int id = m_nextId++;
    LivenessPtr liveness = std::make_shared<Liveness>();
    if (onDestroyed) liveness->setOnDestroyed(onDestroyed, onDestroyedData);
    m_entries.push_back(Entry{ id, win, kind ? kind : "dialog", liveness });
    // Tie the token to the WIDGET, not to the registrant: the sentinel is
    // parked in the window's user_data slot and FLTK deletes it together
    // with the window, so the token flips exactly when the widget dies.
    //
    // The slot has to be free - from here on the registry owns the
    // user_data of a registered window.
    if (win && !win->user_data())
        win->user_data(new DeathSentinel(liveness), true);
    return id;
}

void WidgetRegistry::remove(int id)
{
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == id) { m_entries.erase(m_entries.begin() + i); return; }
    }
}

void WidgetRegistry::removeWindow(Fl_Window *win)
{
    for (size_t i = 0; i < m_entries.size(); ) {
        if (m_entries[i].win == win) m_entries.erase(m_entries.begin() + i);
        else ++i;
    }
}

WidgetRegistry::LivenessPtr WidgetRegistry::liveness(int id)
{
    for (auto &e : m_entries)
        if (e.id == id) return e.liveness;
    return nullptr;
}

void WidgetRegistry::purgeDead()
{
    // An entry whose widget died is dropped here, so no read path can hand
    // out a dangling Fl_Window*. The hooks fire AFTER the entries are gone,
    // so a hook may call back into the registry (remove(), find(), ...)
    // without breaking this loop.
    std::vector<LivenessPtr> dead;
    for (size_t i = 0; i < m_entries.size(); ) {
        const LivenessPtr &tok = m_entries[i].liveness;
        if (!m_entries[i].win || (tok && !tok->alive())) {
            if (tok) dead.push_back(tok);
            m_entries.erase(m_entries.begin() + i);
        } else {
            ++i;
        }
    }
    for (auto &tok : dead) tok->notifyDestroyed();
}

std::vector<WidgetRegistry::Entry> WidgetRegistry::snapshot()
{
    purgeDead();
    return m_entries;
}

WidgetRegistry::Entry *WidgetRegistry::find(int id)
{
    purgeDead();
    for (auto &e : m_entries)
        if (e.id == id && e.win) return &e;
    return nullptr;
}

WidgetRegistry::Entry *WidgetRegistry::findByTitle(const char *title)
{
    if (!title || !*title) return nullptr;
    purgeDead();
    // "appear" means VISIBLE: a dialog that was created and hidden must not
    // satisfy a wait, or an agent would read a stale window as a new one.
    for (auto &e : m_entries)
        if (e.win && e.win->visible() && e.win->label() &&
            std::string(e.win->label()) == title)
            return &e;
    return nullptr;
}

void WidgetRegistry::clear()
{
    purgeDead();   // the widgets that already died still get their hook
    m_entries.clear();
}
