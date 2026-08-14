// Document.h - wraps Fl_Text_Buffer with file I/O, encoding detection/conversion, dirty tracking
#pragma once

#include "core/EncodingCore.h"   // Encoding, EOL, detectEncoding, etc.
#include <FL/Fl_Text_Buffer.H>

#ifndef FL_PATH_MAX
#define FL_PATH_MAX 2048
#endif

// Document
//   Owns one Fl_Text_Buffer and tracks its file path, encoding, and
//   modified state. Used by MainWindow as the model behind each editor.
class Document {
public:
    Document();
    ~Document();

    // File I/O. loadFile() auto-detects the encoding and converts to UTF-8
    // internally. saveFile(path) writes out using the current encoding
    // (settable via setEncoding()).
    bool loadFile(const char *path);
    bool saveFile(const char *path);
    bool saveFile();                 // save to current path (Save As if none)

    // State
    bool isDirty() const { return m_dirty; }
    void markClean();
    const char *filePath() const { return m_filePath; }
    Encoding encoding() const { return m_encoding; }
    const char *encodingName() const;     // human-readable ("UTF-8", "UTF-8 (BOM)", "UTF-16 LE", "GBK", ...)
    void setEncoding(Encoding e);         // change encoding for next save

    // Buffer access
    Fl_Text_Buffer *buffer() const { return m_buffer; }

    // Line-ending style auto-detected on load. New files default to CRLF.
    EOL eol() const { return m_eol; }

    // Trailing-whitespace trimming on save. Set externally (from Config)
    // whenever the document is created or the preference changes.
    bool trimTrailing() const { return m_trimTrailing; }
    void setTrimTrailing(bool on) { m_trimTrailing = on; }

    // Save-time formatting (set from Config alongside trimTrailing).
    void setSaveFormat(bool trimLeadingBlank, bool trimEndingBlank,
                       bool expandTabs, int tabWidth) {
        m_trimLeadingBlank = trimLeadingBlank;
        m_trimEndingBlank = trimEndingBlank;
        m_expandTabs = expandTabs;
        m_tabWidth = tabWidth > 0 ? tabWidth : 4;
    }

    // Read-only flag (large-file mode or external set).
    bool isReadOnly() const { return m_readOnly; }
    void setReadOnly(bool on) { m_readOnly = on; }

    // Detect encoding from the first few bytes of a file (see
    // core/EncodingCore.h - detectEncoding()).

    // Dirty callback target (called by Fl_Text_Buffer)
    static void modifyCallback(int pos, int nInserted, int nDeleted,
                               int nRestyled, const char *deletedText,
                               void *cbArg);

private:
    Fl_Text_Buffer *m_buffer;
    char            m_filePath[FL_PATH_MAX];
    Encoding        m_encoding;
    bool            m_dirty;
    bool            m_readOnly;
    EOL             m_eol;
    bool            m_trimTrailing;
    bool            m_trimLeadingBlank = false;
    bool            m_trimEndingBlank = false;
    bool            m_expandTabs = false;
    int             m_tabWidth = 4;

    void setDirty(bool v);

    // Detect dominant line-ending style from a UTF-8 text block.
    void detectEOL(const char *text, int len);

    // Load file content with a specific encoding into m_buffer
    bool loadWithEncoding(const char *path, Encoding enc);
    bool saveWithEncoding(const char *path, Encoding enc);
};
