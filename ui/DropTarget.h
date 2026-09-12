// DropTarget.h - native Windows IDropTarget for file drag-and-drop
#pragma once

#if defined(_WIN32)

#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <FL/fl_attr.h>  // for FL_OVERRIDE (used on virtual overrides below)
#include <functional>

// PeciaDropTarget - a native Windows IDropTarget that accepts file
// drops (CF_HDROP) and forwards them via a callback.
//
// We implement our own drop target instead of relying on FLTK's
// built-in DnD handling because the combination of border(0) +
// WS_EX_TOOLWINDOW removal + WS_THICKFRAME addition + window
// subclassing in fixTaskbarCb() appears to interfere with FLTK's
// IDropTarget registration. A custom drop target registered AFTER
// all style changes is the most reliable approach.
class PeciaDropTarget : public IDropTarget {
public:
    using OpenFileCallback = std::function<void(const char *)>;

    explicit PeciaDropTarget(HWND hwnd, OpenFileCallback onOpenFile)
        : m_hwnd(hwnd), m_onOpenFile(std::move(onOpenFile)), m_refCount(1) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) FL_OVERRIDE {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() FL_OVERRIDE { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() FL_OVERRIDE {
        LONG c = InterlockedDecrement(&m_refCount);
        if (c == 0) delete this;
        return c;
    }

    // IDropTarget
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *pDataObj,
                                        DWORD /*grfKeyState*/,
                                        POINTL /*pt*/,
                                        DWORD *pdwEffect) FL_OVERRIDE {
        if (!pDataObj || !pdwEffect) return E_INVALIDARG;
        FORMATETC fmt = {};
        fmt.cfFormat = CF_HDROP;
        fmt.tymed = TYMED_HGLOBAL;
        fmt.dwAspect = DVASPECT_CONTENT;
        fmt.lindex = -1;
        HRESULT hr = pDataObj->QueryGetData(&fmt);
        if (hr == S_OK) {
            *pdwEffect = DROPEFFECT_COPY;
            m_accept = true;
        } else {
            *pdwEffect = DROPEFFECT_NONE;
            m_accept = false;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD /*grfKeyState*/,
                                       POINTL /*pt*/,
                                       DWORD *pdwEffect) FL_OVERRIDE {
        if (!pdwEffect) return E_POINTER;
        *pdwEffect = m_accept ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() FL_OVERRIDE {
        m_accept = false;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject *pDataObj,
                                   DWORD /*grfKeyState*/,
                                   POINTL /*pt*/,
                                   DWORD *pdwEffect) FL_OVERRIDE {
        if (pdwEffect) *pdwEffect = m_accept ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (!pDataObj || !m_accept) return S_OK;

        FORMATETC fmt = {};
        fmt.cfFormat = CF_HDROP;
        fmt.tymed = TYMED_HGLOBAL;
        fmt.dwAspect = DVASPECT_CONTENT;
        fmt.lindex = -1;
        STGMEDIUM medium = {};
        HRESULT hr = pDataObj->GetData(&fmt, &medium);
        if (hr != S_OK) return S_OK;

        HDROP hdrop = (HDROP)medium.hGlobal;
        UINT nFiles = DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < nFiles; ++i) {
            UINT len = DragQueryFileW(hdrop, i, nullptr, 0);
            if (len == 0) continue;
            wchar_t *wpath = new wchar_t[len + 1];
            DragQueryFileW(hdrop, i, wpath, len + 1);
            char *upath = new char[(len + 1) * 4];
            WideCharToMultiByte(CP_UTF8, 0, wpath, -1, upath, (int)((len + 1) * 4), nullptr, nullptr);
            if (m_onOpenFile) m_onOpenFile(upath);
            delete[] wpath;
            delete[] upath;
        }

        ReleaseStgMedium(&medium);
        m_accept = false;
        return S_OK;
    }

private:
    HWND        m_hwnd;
    OpenFileCallback m_onOpenFile;
    LONG        m_refCount;
    bool        m_accept = false;
};

#endif // _WIN32
