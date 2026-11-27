// WidgetRegistry.h - the measurement surface for dialogs the host shows on
// a script's behalf (m025 5.1.3 "谁建界面谁登记").
//
// Why a registry and not OS-level inspection: FLTK draws its own controls,
// so an external UIA/Win32 walk sees ONE HWND with no children. The only
// truthful widget tree is the one the host itself holds - so the host
// publishes it here, and the ui.* host ops read it back.
//
// Threading: registration and every read happen on the UI thread (Lua
// scripts and HostOps dispatch both run there). No locking by design -
// adding a lock here would imply a second caller thread that must not exist.
//
// Lifetime: an entry holds a raw Fl_Window*, and FLTK cannot tell us
// whether such a pointer is still a live window - but the WIDGET can tell
// US when it dies. Every registration owns a Liveness token, and the
// registry parks a Fl_Callback_User_Data sentinel in the window's user_data
// slot: FLTK deletes that sentinel in ~Fl_Widget when AUTO_DELETE_USER_DATA
// is set (src/Fl_Widget.cxx:183-184, FL/Fl_Widget.H:190) and
// Fl_Callback_User_Data has a virtual destructor (FL/Fl_Widget.H:96). The
// sentinel therefore dies with the window and flips the token.
//
// Consequence: a registrant that forgets remove() can no longer leave a
// dangling Fl_Window* behind - find(), findByTitle() and snapshot() drop
// the dead entries before handing anything out, so ui.windows / ui.inspect
// / ui.pixel / ui.send never dereference a freed widget.
//
// The sentinel takes over the window's user_data slot, so a REGISTERED
// window must not use user_data() for its own purposes.
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

class Fl_Window;

class WidgetRegistry {
public:
    // "Your widget died" hook (m024 §3 reserves onWidgetDestroyed for
    // exactly this). A plain function pointer: the window is already gone
    // when it fires, so the hook must not be handed a widget pointer.
    using DestroyedCallback = void (*)(void *data);

    // Liveness token of one registration. Shared by the registry and the
    // registrant, so either side can ask "is my widget still alive?".
    class Liveness {
    public:
        // False once the widget was destroyed.
        bool alive() const { return m_alive.load(std::memory_order_acquire); }

        // Install the "your widget died" hook. It fires exactly once, when
        // the widget is destroyed - even if the registration was withdrawn
        // before that.
        void setOnDestroyed(DestroyedCallback cb, void *data)
        { m_cb = cb; m_data = data; }

        // Internal: called by the registry's death sentinel.
        void markDead() { m_alive.store(false, std::memory_order_release); }
        // Internal: fires the hook once, and only once.
        void notifyDestroyed();

    private:
        std::atomic<bool> m_alive{true};
        DestroyedCallback m_cb = nullptr;
        void *m_data = nullptr;
        bool m_notified = false;
    };
    using LivenessPtr = std::shared_ptr<Liveness>;

    struct Entry {
        int          id;      // stable handle handed to the ui.* ops
        Fl_Window   *win;
        std::string  kind;    // "param" / "input" / "select" / ...
        LivenessPtr  liveness; // dies with the widget - see the header note
    };

    static WidgetRegistry &instance();

    // Register a dialog; returns the id the ui.* ops use. The widget's
    // lifetime is tracked from here on (see the header note). onDestroyed,
    // when given, is this registration's "your widget died" hook.
    int  add(Fl_Window *win, const char *kind,
             DestroyedCallback onDestroyed = nullptr, void *onDestroyedData = nullptr);
    void remove(int id);
    void removeWindow(Fl_Window *win);

    // The liveness token of a registration (nullptr for an unknown id).
    LivenessPtr liveness(int id);

    // Registration order (not id order): the newest dialog is last, which is
    // what a caller that just triggered a script wants to inspect.
    std::vector<Entry> snapshot();
    Entry *find(int id);
    Entry *findByTitle(const char *title);

    void clear();

private:
    // Drop the entries whose widget died, then fire their hooks.
    void purgeDead();

    std::vector<Entry> m_entries;
    int m_nextId = 1;
};
