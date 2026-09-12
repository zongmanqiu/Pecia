// ImeInPlace.cpp - shared Windows IME in-place composition (see header).
#include "ui/ImeInPlace.h"

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/x.H>

#include <string>
#include <vector>

#if defined(_WIN32)

namespace {

// Convert a UTF-16 code-unit offset into a UTF-8 byte offset in the same
// UTF-16 string.
int utf16OffsetToUtf8Offset(const wchar_t *s, int utf16Units) {
    int units = 0, bytes = 0;
    for (const wchar_t *p = s; *p && units < utf16Units; ++p) {
        int u = ((*p & 0xFC00) == 0xD800 && (p[1] & 0xFC00) == 0xDC00) ? 2 : 1;
        if (units + u > utf16Units) break;
        unsigned int cp = (u == 2)
            ? (0x10000 + ((*p - 0xD800) << 10) + (p[1] - 0xDC00))
            : (unsigned int)*p;
        bytes += (cp < 0x80) ? 1 : (cp < 0x800) ? 2 : (cp < 0x10000) ? 3 : 4;
        units += u;
        if (u == 2) ++p;
    }
    return bytes;
}

std::string utf16ToUtf8(const wchar_t *s) {
    std::string out;
    if (!s || !*s) return out;
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return out;
    out.resize(n - 1);
    WideCharToMultiByte(CP_UTF8, 0, s, -1, &out[0], n, nullptr, nullptr);
    return out;
}

// Per-editor in-place composition state.
struct CompState {
    Fl_Text_Editor *ed   = nullptr;
    HWND            hwnd = nullptr;
    bool   active   = false;
    int    compStart = -1;
    int    compEnd   = -1;
    std::string compText;         // UTF-8
    int    compCursorBytes = 0;   // caret offset in compText
};

std::vector<CompState *> g_states;

CompState *findState(Fl_Text_Editor *ed) {
    for (auto *s : g_states) if (s->ed == ed) return s;
    return nullptr;
}

struct WindowHook {
    HWND hwnd;
    WNDPROC orig;
};
std::vector<WindowHook> g_hooks;

// --- composition actions ---------------------------------------------------

void compStart(CompState *S) {
    if (!S || !S->ed) return;
    Fl_Text_Buffer *buf = S->ed->buffer();
    if (!buf) return;
    S->active = true;
    S->compStart = S->ed->insert_position();
    S->compEnd = S->compStart;
    int s = 0, e = 0;
    if (buf->selection_position(&s, &e) && e > s) {
        S->compStart = s;   // composition replaces the selection
        S->compEnd = e;
    }
    S->compText.clear();
    S->compCursorBytes = 0;
}

void compUpdate(CompState *S, const wchar_t *comp, int cursorUtf16) {
    if (!S || !S->ed) return;
    Fl_Text_Buffer *buf = S->ed->buffer();
    if (!buf) return;
    if (!S->active) compStart(S);
    if (!S->active) return;

    S->compText = utf16ToUtf8(comp);
    S->compCursorBytes = utf16OffsetToUtf8Offset(comp, cursorUtf16);

    // Park the caret at the composition cursor (clamped to buffer end).
    int caret = S->compStart + S->compCursorBytes;
    if (caret > buf->length()) caret = buf->length();
    S->ed->insert_position(caret);
    S->ed->show_insert_position();
    S->ed->redraw();
}

void compCommit(CompState *S, const wchar_t *result) {
    if (!S || !S->ed) return;
    Fl_Text_Buffer *buf = S->ed->buffer();
    if (!buf) return;
    if (!S->active) {
        S->compStart = S->ed->insert_position();
        S->compEnd = S->compStart;
    }
    std::string s = utf16ToUtf8(result);
    int start = S->compStart, end = S->compEnd;
    if (start < 0) start = S->ed->insert_position();
    if (end < start) end = start;
    int len = buf->length();
    if (start > len) start = len;
    if (end > len) end = len;
    if (start != end || !s.empty())
        buf->replace(start, end, s.c_str());
    S->ed->insert_position(start + (int)s.size());
    S->ed->show_insert_position();
    S->ed->set_changed();
    if (S->ed->when() & FL_WHEN_CHANGED) S->ed->do_callback(FL_REASON_CHANGED);
    // reset
    S->active = false;
    S->compText.clear();
    S->compCursorBytes = 0;
    S->compStart = S->compEnd = -1;
    S->ed->redraw();
}

void compEnd(CompState *S) {
    if (!S) return;
    if (!S->active) return;
    S->active = false;
    S->compText.clear();
    S->compCursorBytes = 0;
    S->compStart = S->compEnd = -1;
    if (S->ed) S->ed->redraw();
}

// --- window-proc hook ------------------------------------------------------

LRESULT CALLBACK imeWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_IME_STARTCOMPOSITION || msg == WM_IME_COMPOSITION || msg == WM_IME_ENDCOMPOSITION) {
        // Route to the focused editor whose top window owns this HWND.
        Fl_Widget *f = Fl::focus();
        Fl_Text_Editor *ed = dynamic_cast<Fl_Text_Editor *>(f);
        if (ed) {
            Fl_Window *w = ed->window();
            for (Fl_Window *ww = w; ww; ww = ww->window()) w = ww;   // topmost window
            if (w && fl_xid(w) == hwnd) {
                CompState *S = findState(ed);
                if (S) {
                    if (msg == WM_IME_STARTCOMPOSITION) { compStart(S); return 0; }
                    if (msg == WM_IME_ENDCOMPOSITION)   { compEnd(S);   return 0; }
                    // WM_IME_COMPOSITION
                    HIMC himc = ImmGetContext(hwnd);
                    bool handled = false;
                    if (himc) {
                        if (lParam & GCS_COMPSTR) {
                            LONG n = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
                            if (n > 0) {
                                std::wstring w2(n / 2, L'\0');
                                ImmGetCompositionStringW(himc, GCS_COMPSTR, &w2[0], n);
                                LONG cp = 0;
                                if (lParam & GCS_CURSORPOS)
                                    ImmGetCompositionStringW(himc, GCS_CURSORPOS, &cp, sizeof(cp));
                                compUpdate(S, w2.c_str(), (int)cp);
                                handled = true;
                            }
                        }
                        if (lParam & GCS_RESULTSTR) {
                            LONG n = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
                            if (n > 0) {
                                std::wstring w2(n / 2, L'\0');
                                ImmGetCompositionStringW(himc, GCS_RESULTSTR, &w2[0], n);
                                compCommit(S, w2.c_str());
                                handled = true;
                            }
                        }
                        ImmReleaseContext(hwnd, himc);
                    }
                    return handled ? 1 : 0;
                }
            }
        }
    }
    for (auto &h : g_hooks)
        if (h.hwnd == hwnd) return CallWindowProcW(h.orig, hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ensureHook(HWND hwnd) {
    if (!hwnd) return;
    for (auto &h : g_hooks) if (h.hwnd == hwnd) return;
    WindowHook h;
    h.hwnd = hwnd;
    h.orig = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
    g_hooks.push_back(h);
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)&imeWindowProc);
}

}  // namespace

namespace ImeInPlace {

void attach(Fl_Text_Editor *ed) {
    if (!ed) return;
    HWND hwnd = 0;
    Fl_Window *w = ed->window();
    for (Fl_Window *ww = w; ww; ww = ww->window()) w = ww;   // topmost
    if (w) hwnd = fl_xid(w);
    if (!hwnd) return;
    if (!findState(ed)) {
        CompState *S = new CompState();
        S->ed = ed;
        S->hwnd = hwnd;
        g_states.push_back(S);
    }
    ensureHook(hwnd);
}

void detach(Fl_Text_Editor *ed) {
    if (!ed) return;
    CompState *S = findState(ed);
    if (S) {
        compEnd(S);
        // Keep S (so re-focus gets a stable state); it will be freed when the
        // editor is destroyed. To avoid leaks we rely on attach() re-mapping
        // an existing state. (Editor destruction is out of scope for the hook.)
    }
}

void release(Fl_Text_Editor *ed) {
    if (!ed) return;
    for (size_t i = 0; i < g_states.size(); ++i) {
        if (g_states[i]->ed == ed) {
            delete g_states[i];
            g_states.erase(g_states.begin() + i);
            break;
        }
    }
    // Drop window hooks that no editor state uses anymore (the HWND may be
    // gone or reused; leaving the entry would both leak and risk routing
    // WM_IME_* of an unrelated window through our proc).
    for (size_t i = 0; i < g_hooks.size();) {
        bool used = false;
        for (auto *S : g_states)
            if (S->hwnd == g_hooks[i].hwnd) { used = true; break; }
        if (!used)
            g_hooks.erase(g_hooks.begin() + i);
        else
            ++i;
    }
}

bool compositionActive(Fl_Text_Editor *ed) {
    CompState *S = findState(ed);
    return S && S->active;
}

}  // namespace ImeInPlace

#endif // _WIN32
