// SettingsProvider.h - Pure virtual interface for editor settings access
#pragma once

// SettingsProvider
//   Abstract interface that decouples UI components from the concrete
//   Config class. Components (Editor, FindReplace, etc.) hold a
//   SettingsProvider* instead of Config*, allowing them to be tested
//   with mock implementations and keeping the dependency direction
//   pointing away from concrete storage.
//
//   Config (core/Config.h) implements this interface.
class SettingsProvider {
public:
    virtual ~SettingsProvider() = default;

    // Editor options
    virtual int  getTabWidth() const = 0;
    virtual bool getAutoIndent() const = 0;
    virtual bool getDetectUrls() const = 0;

    // Font
    virtual void getFont(int &font, int &size) const = 0;

    // View toggles
    virtual bool getHighlightCurrentLine() const = 0;
    virtual bool getShowWhitespace() const = 0;
    virtual bool getWrap() const = 0;
    virtual bool getMultiTab() const = 0;
    virtual int  getLongLineMarker() const = 0;
    virtual bool getAlwaysOnTop() const = 0;
    virtual bool getShowStatusbar() const = 0;
    virtual bool getTrimTrailingWhitespace() const = 0;

    // Language / locale
    virtual void getLang(char *buf, int len, const char *fallback) const = 0;

    // Window geometry
    virtual void getWindow(int &x, int &y, int &w, int &h, int &flags) const = 0;
};
