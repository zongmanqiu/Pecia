// ImeInPlace.cpp - shared Windows IME in-place composition (see header).
#include "ui/ImeInPlace.h"

#include <FL/Fl.H>
#include <FL/Fl_Widget.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>

#include <cstring>

#if defined(_WIN32)

#include <FL/x.H>
#include <imm.h>

#include "ui/NativeSubclass.h"

#include <string>
#include <vector>

namespace {

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

struct HostEntry {
    ImeInPlace::Host *host;
    HWND              hwnd;
};
std::vector<HostEntry> g_hosts;

LRESULT CALLBACK imeWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

ImeInPlace::Host *focusedHost(HWND hwnd) {
    Fl_Widget *f = Fl::focus();
    if (!f) return nullptr;
    for (auto &e : g_hosts)
        if (e.host->imeWidget() == f && e.hwnd == hwnd) return e.host;
    return nullptr;
}

void ensureHook(HWND hwnd) {
    // One chain for every subclass level, ours included: ui/NativeSubclass.h
    // keeps the full per-window chain, so installing here - before, between
    // or after any frame proc, and however many times - can never point two
    // levels at each other. (The old private g_hooks bookkeeping formed
    // exactly such a cycle with a re-run DialogBase::show(): measured
    // 2026-10-22, stack overflow on the Font dialog's button bar.)
    nativeSubclass::install(hwnd, (WNDPROC)&imeWindowProc);
}

LRESULT CALLBACK imeWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_IME_SETCONTEXT) {
        if (focusedHost(hwnd))
            lParam &= ~ISC_SHOWUICOMPOSITIONWINDOW;
        return nativeSubclass::forward(hwnd, (WNDPROC)&imeWindowProc, msg,
                                       wParam, lParam);
    }

    if (msg == WM_IME_STARTCOMPOSITION) {
        if (ImeInPlace::Host *host = focusedHost(hwnd)) {
            host->imeCompositionStart();
            return 0;
        }
    } else if (msg == WM_IME_ENDCOMPOSITION) {
        if (ImeInPlace::Host *host = focusedHost(hwnd)) {
            host->imeCompositionEnd();
            return 0;
        }
    } else if (msg == WM_IME_COMPOSITION) {
        if (ImeInPlace::Host *host = focusedHost(hwnd)) {
            HIMC himc = ImmGetContext(hwnd);
            bool handled = false;
            if (himc) {
                if (lParam & GCS_COMPSTR) {
                    LONG n = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
                    if (n >= 0) {
                        std::wstring w(static_cast<size_t>(n / 2), L'\0');
                        if (n > 0)
                            ImmGetCompositionStringW(himc, GCS_COMPSTR, &w[0], n);
                        std::string utf8 = utf16ToUtf8(w.c_str());
                        LONG cp = 0;
                        if (lParam & GCS_CURSORPOS)
                            ImmGetCompositionStringW(himc, GCS_CURSORPOS, &cp, sizeof(cp));
                        host->imeCompositionUpdate(
                            utf8.c_str(),
                            utf16OffsetToUtf8Offset(w.c_str(), (int)cp));
                        handled = true;
                    }
                }
                if (lParam & GCS_RESULTSTR) {
                    LONG n = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
                    if (n > 0) {
                        std::wstring w(static_cast<size_t>(n / 2), L'\0');
                        ImmGetCompositionStringW(himc, GCS_RESULTSTR, &w[0], n);
                        host->imeCompositionCommit(utf16ToUtf8(w.c_str()).c_str());
                        handled = true;
                    }
                }
                ImmReleaseContext(hwnd, himc);
            }
            return handled ? 1 : 0;
        }
    }

    return nativeSubclass::forward(hwnd, (WNDPROC)&imeWindowProc, msg,
                                   wParam, lParam);
}

}  // namespace

#endif  // _WIN32

namespace ImeInPlace {

#if defined(_WIN32)

void attach(Host *host) {
    if (!host) return;
    Fl_Widget *w = host->imeWidget();
    if (!w || !w->window()) return;
    Fl_Window *top = w->window()->top_window();
    if (!top || !top->shown()) return;
    HWND hwnd = fl_xid(top);
    if (!hwnd) return;
    for (auto &e : g_hosts) {
        if (e.host == host) {
            e.hwnd = hwnd;
            ensureHook(hwnd);
            return;
        }
    }
    HostEntry e;
    e.host = host;
    e.hwnd = hwnd;
    g_hosts.push_back(e);
    ensureHook(hwnd);
}

void detach(Host *host) {
    if (!host) return;
    for (auto &e : g_hosts) {
        if (e.host == host) {
            host->imeCompositionEnd();
            return;
        }
    }
}

void release(Host *host) {
    if (!host) return;
    for (size_t i = 0; i < g_hosts.size(); ++i) {
        if (g_hosts[i].host == host) {
            g_hosts.erase(g_hosts.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    // Collect the windows this host was attached to before dropping entries.
    std::vector<HWND> hooked;
    for (size_t i = 0; i < g_hosts.size();) {
        if (g_hosts[i].host == host) {
            hooked.push_back(g_hosts[i].hwnd);
            g_hosts.erase(g_hosts.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    for (HWND h : hooked) {
        bool used = false;
        for (auto &e : g_hosts)
            if (e.hwnd == h) { used = true; break; }
        if (!used) nativeSubclass::uninstall(h, (WNDPROC)&imeWindowProc);
    }
}

#else

void attach(Host *) {}
void detach(Host *) {}
void release(Host *) {}

#endif  // _WIN32

void drawComposition(const char *text, int cursorBytes,
                     int x, int top, int lineH,
                     Fl_Color textColor, Fl_Color caretColor, bool caretVisible,
                     Fl_Font font, int fontSize, Fl_Window *win,
                     int clipX, int clipY, int clipW, int clipH) {
    if (!text || !*text) return;
    if (clipW <= 0 || clipH <= 0) return;
    int len = (int)strlen(text);
    if (cursorBytes < 0) cursorBytes = 0;
    if (cursorBytes > len) cursorBytes = len;

    fl_push_clip(clipX, clipY, clipW, clipH);
    fl_font(font, fontSize);
    fl_color(textColor);
    fl_draw(text, x, top + lineH - fl_descent());
    if (caretVisible) {
        int cw = (int)fl_width(text, cursorBytes);
        fl_color(caretColor);
        fl_rectf(x + cw, top, 2, lineH);
        fl_set_spot(font, fontSize, x + cw, top + lineH - 1, clipW, clipH, win);
    }
    fl_pop_clip();
}

}  // namespace ImeInPlace
