// ThemeWidgets.h - Themed widget classes for the standalone tools.
// Mirrors the main window's widget styling: flat editors/displays,
// 10px theme-colored scrollbars drawn the same way as the main
// editor's custom scrollbars (Editor::drawCustomScrollbar), bordered
// inputs and the accent selection color on buttons.
#pragma once

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/fl_draw.H>
#include <initializer_list>

#include "ui/BandColourSource.h"
#include <string>
#include <vector>

#include "core/Theme.h"
#include "ui/BlinkInput.h"   // ThemedInput 继承它：光标闪烁与主编辑器同周期
#include "ui/ImeInPlace.h"   // ThemedTextEditor 直接继承 ImeInPlace::Host

namespace {

// Shared scrollbar color lookup: track = background1, thumb = background2.
// All scrollbar implementations must use this to stay consistent.
inline void scrollbarColors(const ThemeColors &tc,
                           Fl_Color &track, Fl_Color &thumb) {
    track = tc.background1;
    thumb = tc.background2;
}

// Apply the main window's scrollbar style (10px, no arrows, flat,
// theme thumb/track) to one scrollbar.
inline void styleToolScrollbar(Fl_Scrollbar *sb, uchar type,
                               const ThemeColors &tc) {
    if (!sb) return;
    sb->type(type);
    sb->box(FL_FLAT_BOX);
    Fl_Color track, thumb;
    scrollbarColors(tc, track, thumb);
    sb->color(track);
    sb->selection_color(thumb);
}

// Thumb geometry for a custom-drawn scrollbar.
//
// ★ It MUST match the hit test exactly, otherwise what is drawn and what can
//   be grabbed are two different rectangles (seen as "must press under the
//   slider to drag it" when the thumb is in the upper half).
//
// Authoritative FLTK formulas (Fl_Slider.cxx:130-140 draw / :256-259 handle):
//     S  = int(slider_size * L + 0.5)      thumb length (L = track length)
//     T  = (horizontal ? H : W)/2 + 1       minimum thumb length
//     if (S < T) S = T;
//     xx = int(val * (L - S) + 0.5)         thumb origin
//
// ★ PATCH interplay: upstream Fl_Scrollbar carves an arrow-sized strip
//   (crossSize px per end) off the track for hit-test AND drawing when
//   trackLen >= 3*crossSize. That desynchronized draw from hit by crossSize
//   pixels (the reported off-by-10px grab offset). Pecia patches
//   Fl_Scrollbar.cxx (patches/fltk-1.4.5/) to remove the arrow zones
//   entirely -- custom scrollbars paint the full rect with no arrows -- so
//   the full-track formulas below are again authoritative for BOTH sides.
//   If you ever revert the FLTK patch, this function must carve the same
//   arrow strips.
//
// A press inside keeps `offcenter`, so dragging to the bottom clamps to
// L-S and reaches maximum; a press outside gets `offcenter` clamped to S.
//
// History: this used `max(int(L*slider_size), 20)`. That 20px floor made the
// drawn thumb LARGER than FLTK's hit area (which shrinks to T=6px for long
// documents). Measured at 800x600 / Consolas 16 / wrap on: a 2000-line
// document had 10 of 20 thumb rows dead (50%). That is the reported bug.
// The caller resolves the axis-specific dimensions: pass the track length as
// `trackLen` and the across-axis thickness as `crossSize`. That makes this
// function identical for both axes, so there is no `horizontal` parameter
// (an unused one would trip C4100).
inline void scrollbarThumbGeometry(const Fl_Scrollbar *sb,
                                   int trackOrigin, int trackLen,
                                   int crossSize,
                                   int *out_thumb_len, int *out_thumb_pos) {
    const double minv = sb->minimum();
    const double maxv = sb->maximum();
    const double v = sb->value();
    const float sl = sb->slider_size();

    int S = (int)(sl * trackLen + 0.5f);
    const int T = crossSize / 2 + 1;
    if (S < T) S = T;
    if (S > trackLen) S = trackLen;   // guard: never overflow a very short track

    double val;
    if (minv == maxv) {
        val = 0.5;
    } else {
        val = (v - minv) / (maxv - minv);
        if (val > 1.0) val = 1.0;
        else if (val < 0.0) val = 0.0;
    }
    const int xx = (int)(val * (trackLen - S) + 0.5);

    *out_thumb_len = S;
    *out_thumb_pos = trackOrigin + xx;
}

// Draw one scrollbar exactly like the main editor does
// (Editor::drawCustomScrollbar): plain track + thumb rectangles in
// the theme colors, no FLTK native grooves/slider details.
inline void drawToolScrollbar(Fl_Scrollbar *sb, bool horizontal,
                              const ThemeColors &tc) {
    if (!sb || !sb->visible_r()) return;

    int sx = sb->x();
    int sy = sb->y();
    int sw = sb->w();
    int sh = sb->h();

    Fl_Color track, thumb;
    scrollbarColors(tc, track, thumb);

    fl_color(track);
    fl_rectf(sx, sy, sw, sh);

    int thumb_len = 0, thumb_pos = 0;
    if (horizontal)
        scrollbarThumbGeometry(sb, sx, sw, sh, &thumb_len, &thumb_pos);
    else
        scrollbarThumbGeometry(sb, sy, sh, sw, &thumb_len, &thumb_pos);

    fl_color(thumb);
    if (horizontal)
        fl_rectf(thumb_pos, sy, thumb_len, sh);
    else
        fl_rectf(sx, thumb_pos, sw, thumb_len);
}

} // namespace

// Flat button that fills with selection_color() on hover / while pressed,
// mirroring the menu bar's item highlight (HoverMenuBar) and the title
// bar buttons. Callers style it like any Fl_Button: set color() for the
// resting background, selection_color() for the hover/pressed fill, and
// labelcolor() for the text. A 1px border (FL_BORDER_BOX) keeps the
// button look distinct from plain chrome background.
class HoverButton : public Fl_Button {
public:
    // Horizontal padding on each side of the label (total = 2x). Used by
    // fit() so every auto-fitted button shares the exact same width rule.
    static constexpr int kPadX = 12;
    // Minimum auto-fitted width, so a short label still yields a tappable
    // button (all windows agree on this one value).
    static constexpr int kMinW = 48;

    HoverButton(int X, int Y, int W, int H, const char *l = nullptr)
        : Fl_Button(X, Y, W, H, l), m_pressColor(0) {
        box(FL_BORDER_BOX);
    }

    // Pressed-state fill (theme highlight2). 0 = derive via fl_darker().
    void setPressColor(Fl_Color c) { m_pressColor = c; }

    // Width that fits the current label (text width + 2*padX, clamped to
    // kMinW). Works with any label/font already set on this button.
    int fitWidth() const {
        Fl_Button *me = const_cast<HoverButton *>(this);
        int tw = 0, th = 0;
        me->measure_label(tw, th);
        int w = tw + 2 * kPadX;
        if (w < kMinW) w = kMinW;
        return w;
    }

    // Resize to fit the label (height unchanged). Convenient for buttons
    // whose final x is computed by the caller (right-aligned rows).
    void fit() {
        resize(x(), y(), fitWidth(), h());
    }

    void draw() FL_OVERRIDE {
        Fl_Color c = color();
        if (Fl::belowmouse() == this || Fl::pushed() == this) {
            c = selection_color();
            // Distinct press feedback: while actually held down, use the
            // caller-set m_pressColor (theme highlight2) when provided,
            // else darken (fl_darker = 67% base + 33% black) so a click
            // reads as a deeper press than a plain hover.
            if (Fl::pushed() == this) {
                if (m_pressColor != 0)
                    c = m_pressColor;
                else
                    c = fl_darker(c);
            }
        }
        draw_box(box(), c);
        draw_label();
        if (Fl::focus() == this) draw_focus();
    }

    int handle(int event) FL_OVERRIDE {
        // Redraw on enter/leave so the hover fill tracks the mouse
        // immediately (Fl_Button only repaints on press by default).
        if (event == FL_ENTER || event == FL_LEAVE) redraw();
        return Fl_Button::handle(event);
    }

private:
    Fl_Color m_pressColor = 0;   // pressed fill (theme highlight2); 0=derive
};

// Layout a right-aligned row of buttons across [barX, rightEdge] (the
// chrome bottom button bar of a dialog). Each button is first sized to its
// label (HoverButton::fit), then placed right-to-left with `gap` between
// them, ending `margin` from rightEdge, vertically centered at `barCenterY`.
// Accepts Fl_Button* so callers can pass members typed as the base class;
// every button must actually be a HoverButton (they all are in Pecia).
inline void fitButtonRow(std::initializer_list<Fl_Button *> btns,
                         int rightEdge, int barCenterY, int margin, int gap) {
    // Size every button to its label first.
    for (Fl_Button *b : btns)
        if (b) static_cast<HoverButton *>(b)->fit();
    // Place right-to-left.
    int x = rightEdge - margin;
    // Walk the list in reverse to lay out from right to left.
    const auto *vec = btns.begin();
    for (size_t i = btns.size(); i-- > 0;) {
        Fl_Button *b = vec[i];
        if (!b) continue;
        int w = b->w();
        b->position(x - w, barCenterY - b->h() / 2);
        x -= w + gap;
    }
}

// 对话框底部按钮条 —— Options / 确认框 / AI 聊天 / Lua 工具窗共用的那一套：
//   · 通栏 Fl_Group，底色 background2（比内容区深一档，形成"页脚"分层）
//   · HoverButton：静止底色=background2（与条同色，只剩 1px 边框）、
//     悬停/按下=highlight2、文字 text1、宽度按标签自适应
//   · 右对齐；**btns 的顺序就是从左到右的视觉顺序**，最后一个落在最右边，
//     因此"取消/否定"类按钮必须放在数组末尾（全项目统一：[OK][Cancel]，
//     与 Windows 习惯一致）。本函数内部倒序遍历，切勿再按"第一个在最右"理解。
// 之所以做成函数：以前每个对话框各写一遍，FontDialog 就写歪了（固定 96px 宽 +
// background3 底色 + highlight1 悬停 + 按钮顺序反了）。
inline Fl_Group *createDialogButtonBar(int x, int y, int w, int h,
                                       const ThemeColors &tc, int fontSize,
                                       const std::vector<HoverButton *> &btns,
                                       int margin, int gap) {
    Fl_Group *bar = new Fl_Group(x, y, w, h);
    bar->box(FL_FLAT_BOX);
    bar->color(tc.background2);
    bar->begin();
    for (HoverButton *b : btns) {
        if (!b) continue;
        b->labelsize(fontSize);
        b->labelcolor(tc.text1);
        b->color(tc.background2);
        b->selection_color(tc.highlight2);
        b->setPressColor(tc.highlight2);
        b->fit();                       // 宽度按标签算，全程序同一个规则
        bar->add(b);
    }
    int bx = x + w - margin;
    for (size_t i = btns.size(); i-- > 0;) {
        HoverButton *b = btns[i];
        if (!b) continue;
        b->position(bx - b->w(), y + (h - b->h()) / 2);
        bx -= b->w() + gap;
    }
    bar->end();
    bar->resizable(nullptr);
    return bar;
}

// Dropdown styled like the Options dialog's Tab width / Auto save
// choices: rectangular FL_BORDER_BOX with a divider line and arrow.
// Fl_Choice would hardcode its own box type otherwise.
class SettingsChoice : public Fl_Choice {
public:
    SettingsChoice(int X, int Y, int W, int H, const char *L = nullptr)
        : Fl_Choice(X, Y, W, H, L) {}

    void draw() FL_OVERRIDE {
        // Draw the widget box using our own box type (not Fl_Choice's
        // hardcoded one).
        draw_box(box(), color());

        // Arrow area
        int dx = Fl::box_dx(box());
        int dy = Fl::box_dy(box());
        int H = h() - 2 * dy;
        int W = 20;
        int X = x() + w() - W - dx;
        int Y = y() + dy;
        int active = active_r();
        Fl_Color arrow_color = active ? labelcolor() : fl_inactive(labelcolor());

        // Arrow divider: vertical line
        int x1 = X;
        int y1 = y() + dy;
        int y2 = y() + h() - dy;
        fl_color(g_borderColour);
        fl_yxline(x1, y1, y2);

        // Arrow
        Fl_Rect ab(X, Y, W, H);
        ab.inset(2, 0, 2, 0);
        fl_draw_arrow(ab, FL_ARROW_CHOICE, FL_ORIENT_NONE, arrow_color);

        // Selected text
        W += 2 * dx;
        if (mvalue()) {
            Fl_Menu_Item m = *mvalue();
            if (active) m.activate(); else m.deactivate();
            int xx = x() + dx, yy = y() + dy + 1, ww = w() - W, hh = H - 2;
            fl_push_clip(xx, yy, ww, hh);
            fl_draw_shortcut = 2;
            m.draw(xx, yy, ww, hh, this, Fl::focus() == this);
            fl_draw_shortcut = 0;
            fl_pop_clip();
        }

        draw_label();
    }
};

// Single-line input styled like the main window's editor/inputs: bordered,
// theme background/text and a cursor colour matching the main window's
// textPrimary. Replaces bare Fl_Input so every dialog input looks (and has
// the same caret colour as) the main editor.
class ThemedInput : public BlinkInput {
public:
    ThemedInput(int x, int y, int w, int h, const ThemeColors &tc,
                const char *lbl = nullptr)
        : BlinkInput(x, y, w, h, lbl) {
        box(FL_BORDER_BOX);
        color(tc.background1);
        textcolor(tc.text1);
        cursor_color(tc.text1);
        selection_color(tc.highlight1);
    }

    // Re-apply colours from a (possibly newly reloaded) theme.
    void themify(const ThemeColors &tc) {
        color(tc.background1);
        textcolor(tc.text1);
        cursor_color(tc.text1);
        selection_color(tc.highlight1);
    }
};

// ---------------------------------------------------------------------------
// 搜索框(ShortcutDialog / FontDialog 共用;提取自 ui/ShortcutDialog.cpp)。
// 组成:左侧 24px 内缩的输入框 boxtype(文本从图标之后开始)+ 在输入框
// **自身 draw() 里**画的手绘放大镜 + 可选的输入内浅色提示字。见开发指南
// 「UI 改动规约」:UI 元素只复用不复制——再做第三个搜索框时直接用
// SearchInput,不要再抄一份。
// ---------------------------------------------------------------------------
constexpr int kSearchFieldH = 22;   // 输入框高度(与 find/go-to 栏输入框同高)
constexpr int kSearchIconW  = 24;   // 左侧图标区内缩(文本起点偏移)

// 与 FL_BORDER_BOX 逐像素相同、但左内缩 kSearchIconW 的 boxtype。
// FL_FREE_BOXTYPE 槽位进程内只注册一次(inline 函数的局部静态全 TU 共享)。
inline Fl_Boxtype searchInputBoxtype() {
    static const Fl_Boxtype b = [] {
        Fl_Boxtype t = (Fl_Boxtype)FL_FREE_BOXTYPE;
        Fl::set_boxtype(t, Fl::get_boxtype(FL_BORDER_BOX),
                        kSearchIconW, 1, kSearchIconW + 2, 2, nullptr);
        return t;
    }();
    return b;
}

// 搜索输入框:BlinkInput + 左内缩 boxtype,放大镜画在本控件内部。
// 图标为什么不能做成兄弟控件:输入框每次 FL_DAMAGE_ALL 重绘(获焦、
// 光标闪烁、输入)都会走 Fl_Input::draw() 的 draw_box(),用背景色把
// 整个矩形(含左内缩)不透明填掉,兄弟图标的像素随即被盖掉——
// FontDialog 曾因此「图标消失」(它构造时把焦点给了输入框,500ms 一次的
// 闪烁重绘把图标擦了;快捷键对话框只是因为打开时不自动聚焦才没暴露。
// 实测 2026-10-22,temp/probe/ 三阶段截屏:0.4s 时图标 136 个非背景像素,
// 点击/闪烁后只剩 24 个)。画在本控件内,每次重绘都连着重画,永不丢。
class SearchInput : public BlinkInput {
public:
    SearchInput(int X, int Y, int W, int H, const char *l = nullptr)
        : BlinkInput(X, Y, W, H, l) {
        box(searchInputBoxtype());
    }

    // 放大镜颜色(主题 text2)。
    void iconColor(Fl_Color c) { m_icon = c; redraw(); }

    void draw() FL_OVERRIDE {
        BlinkInput::draw();
        // 几何与旧兄弟控件版 SearchIconBox(16x16 图标框 @(x+5,y+4))
        // 逐像素一致:镜圈内缩 1px,柄从圈缘延伸到内缩右缘。
        fl_color(m_icon);
        fl_line_style(FL_SOLID, 2);
        fl_arc(x() + 6, y() + 5, 11, 11, 0, 360);          // lens
        fl_line(x() + 15, y() + 14, x() + 20, y() + 19);   // handle
        fl_line_style(FL_SOLID, 1);
    }

private:
    Fl_Color m_icon = FL_BLACK;
};

// Text display styled like the main window's editor panes, including
// custom-drawn scrollbars identical to Editor::drawCustomScrollbar.
class ThemedTextDisplay : public Fl_Text_Display,
                          public windowChrome::BandColourSource {
public:
    ThemedTextDisplay(int x, int y, int w, int h, const ThemeColors &tc)
        : Fl_Text_Display(x, y, w, h), m_tc(tc) {
        box(FL_FLAT_BOX);
        color(tc.background1);
        textcolor(tc.text1);
        selection_color(tc.highlight2);
        scrollbar_size(10);
        styleToolScrollbar(mVScrollBar, FL_VERT_SLIDER, tc);
        styleToolScrollbar(mHScrollBar, FL_HOR_SLIDER, tc);
    }

    // The scrollbars are painted by drawToolScrollbar() below, not by FLTK's
    // box, so the widget tree reports the text-area colour there. The
    // non-client band abuts the right-hand scrollbar and would otherwise show
    // the text background instead of the scrollbar track (measured: band
    // #000000 beside a #323232 track on 208 of 606 rows).
    bool bandColourAt(int x, int y, unsigned int *colour) const override {
        if (!colour) return false;
        Fl_Scrollbar *bars[2] = { mVScrollBar, mHScrollBar };
        for (Fl_Scrollbar *sb : bars) {
            if (!sb || !sb->visible()) continue;
            if (x < sb->x() || x >= sb->x() + sb->w()) continue;
            if (y < sb->y() || y >= sb->y() + sb->h()) continue;
            Fl_Color track, thumb;
            scrollbarColors(m_tc, track, thumb);
            const bool vert = (sb == mVScrollBar);
            int len = 0, pos = 0;
            if (vert) scrollbarThumbGeometry(sb, sb->y(), sb->h(), sb->w(),
                                             &len, &pos);
            else      scrollbarThumbGeometry(sb, sb->x(), sb->w(), sb->h(),
                                             &len, &pos);
            const int along = vert ? y : x;
            *colour = static_cast<unsigned int>(
                (along >= pos && along < pos + len) ? thumb : track);
            return true;
        }
        return false;   // text area: let the normal tree walk resolve it
    }

    void draw() FL_OVERRIDE {
        Fl_Text_Display::draw();
        drawToolScrollbar(mVScrollBar, false, m_tc);
        drawToolScrollbar(mHScrollBar, true, m_tc);
    }

private:
    const ThemeColors &m_tc;
};

// Text editor styled like the main window's editor panes.
class ThemedTextEditor : public Fl_Text_Editor,
                         public ImeInPlace::Host,
                         public windowChrome::BandColourSource {
public:
    ThemedTextEditor(int x, int y, int w, int h, const ThemeColors &tc,
                     bool lineHighlight = true)
        : Fl_Text_Editor(x, y, w, h), m_tc(tc), m_lineHighlightOn(lineHighlight) {
        box(FL_FLAT_BOX);
        color(tc.background1);
        textcolor(tc.text1);
        selection_color(tc.highlight2);
        cursor_color(tc.text1);
        // 与主编辑器一致：交给 FLTK 画 2px、无端帽的光标（见 Editor 构造函数）。
        // 这里原本自绘"擦 5px + 画 3px"，既遮字又会和 FLTK 原生光标（HEAVY 3px）
        // 叠出残影 —— 已删。
        cursor_style(Fl_Text_Display::SIMPLE_CURSOR);
        scrollbar_size(10);
        styleToolScrollbar(mVScrollBar, FL_VERT_SLIDER, tc);
        styleToolScrollbar(mHScrollBar, FL_HOR_SLIDER, tc);
    }

    ~ThemedTextEditor() override {
#if defined(_WIN32)
        // Unregister from the shared IME hook before the widget goes away.
        ImeInPlace::release(this);
#endif
    }

    // ---- ImeInPlace::Host (same composition path as the main editor) ----
    Fl_Widget *imeWidget() override { return this; }

    // Same reason as ThemedTextDisplay: the scrollbars are hand-painted.
    bool bandColourAt(int x, int y, unsigned int *colour) const override {
        if (!colour) return false;
        Fl_Scrollbar *bars[2] = { mVScrollBar, mHScrollBar };
        for (Fl_Scrollbar *sb : bars) {
            if (!sb || !sb->visible()) continue;
            if (x < sb->x() || x >= sb->x() + sb->w()) continue;
            if (y < sb->y() || y >= sb->y() + sb->h()) continue;
            Fl_Color track, thumb;
            scrollbarColors(m_tc, track, thumb);
            const bool vert = (sb == mVScrollBar);
            int len = 0, pos = 0;
            if (vert) scrollbarThumbGeometry(sb, sb->y(), sb->h(), sb->w(),
                                             &len, &pos);
            else      scrollbarThumbGeometry(sb, sb->x(), sb->w(), sb->h(),
                                             &len, &pos);
            const int along = vert ? y : x;
            *colour = static_cast<unsigned int>(
                (along >= pos && along < pos + len) ? thumb : track);
            return true;
        }
        return false;
    }

    void imeCompositionStart() override {
        if (!buffer()) return;
        m_compActive = true;
        m_compStart = insert_position();
        m_compEnd = m_compStart;
        int s = 0, e = 0;
        if (buffer()->selection_position(&s, &e) && e > s) {
            m_compStart = s;
            m_compEnd = e;
        }
        m_compText.clear();
        m_compCursorBytes = 0;
    }

    void imeCompositionUpdate(const char *utf8, int cursorBytes) override {
        if (!buffer()) return;
        if (!m_compActive) imeCompositionStart();
        if (!m_compActive) return;
        m_compText = utf8 ? utf8 : "";
        if (cursorBytes < 0) cursorBytes = 0;
        if (cursorBytes > (int)m_compText.size())
            cursorBytes = (int)m_compText.size();
        m_compCursorBytes = cursorBytes;
        int caret = m_compStart + m_compCursorBytes;
        if (caret > buffer()->length()) caret = buffer()->length();
        insert_position(caret);
        show_insert_position();
        redraw();
    }

    void imeCompositionCommit(const char *utf8) override {
        if (!buffer()) return;
        if (!m_compActive) {
            m_compStart = insert_position();
            m_compEnd = m_compStart;
        }
        std::string s = utf8 ? utf8 : "";
        int start = m_compStart;
        int end = m_compEnd;
        if (start < 0) start = insert_position();
        if (end < start) end = start;
        int len = buffer()->length();
        if (start > len) start = len;
        if (end > len) end = len;
        if (start != end || !s.empty())
            buffer()->replace(start, end, s.c_str());
        insert_position(start + (int)s.size());
        show_insert_position();
        set_changed();
        if (when() & FL_WHEN_CHANGED) do_callback(FL_REASON_CHANGED);
        m_compActive = false;
        m_compText.clear();
        m_compCursorBytes = 0;
        m_compStart = m_compEnd = -1;
        syncHighlightBuffer();
        redraw();
    }

    void imeCompositionEnd() override {
        if (!m_compActive) return;
        m_compActive = false;
        m_compText.clear();
        m_compCursorBytes = 0;
        m_compStart = m_compEnd = -1;
        redraw();
    }

    // ---- Current-line highlight via style buffer (same as Editor.cpp) ----

    // (Re)build the style buffer: 'A' = normal, 'B' = current line.
    void applyHighlight() {
        if (!buffer() || !m_lineHighlightOn) return;
        int len = buffer()->length();
        if (len <= 0) { m_lastLineStart = -1; return; }
        if (!m_styleBuf) m_styleBuf = new Fl_Text_Buffer(len > 0 ? len : 1);
        std::string fill(len, 'A');
        m_styleBuf->text(fill.c_str());
        m_styleTable[0].color   = textcolor();
        m_styleTable[0].font    = textfont();
        m_styleTable[0].size    = textsize();
        m_styleTable[0].attr    = 0;
        m_styleTable[0].bgcolor = color();
        m_styleTable[1].color   = textcolor();
        m_styleTable[1].font    = textfont();
        m_styleTable[1].size    = textsize();
        m_styleTable[1].attr    = Fl_Text_Display::ATTR_BGCOLOR;
        m_styleTable[1].bgcolor = m_tc.highlight1;
        if (!m_hlDataSet) {
            highlight_data(m_styleBuf, m_styleTable, 2, 'A', 0, 0);
            m_hlDataSet = true;
        }
        // Mark the current line 'B'.
        int pos = insert_position();
        if (pos < 0) pos = 0;
        if (pos > len) pos = len;
        int lineStart = buffer()->line_start(pos);
        int lineEnd   = buffer()->line_end(pos);
        if (lineEnd > len) lineEnd = len;
        m_lastLineStart = lineStart;
        if (lineEnd > lineStart) {
            std::string hl(lineEnd - lineStart, 'B');
            m_styleBuf->replace(lineStart, lineEnd, hl.c_str());
        }
        redraw();
    }

    // Rebuild the style buffer to match the current text length (call
    // when the text changes size). Same as Editor::syncLineHighlightBuffer.
    void syncHighlightBuffer() {
        if (!m_lineHighlightOn || !buffer()) return;
        m_lastLineStart = -1;
        applyHighlight();
    }

    // Mark the line under the cursor 'B' (call when the cursor moves to
    // another line). Same as Editor::updateLineHighlight.
    void refreshHighlight() {
        if (!m_lineHighlightOn || !buffer() || !m_styleBuf) return;
        int pos = insert_position();
        int len = buffer()->length();
        if (pos < 0) pos = 0;
        if (pos > len) pos = len;
        int curLineStart = buffer()->line_start(pos);
        if (curLineStart == m_lastLineStart) return;
        // Revert the previous line to 'A'.
        if (m_lastLineStart >= 0 && m_lastLineStart < len) {
            int oldEnd = buffer()->line_end(m_lastLineStart);
            if (oldEnd > len) oldEnd = len;
            if (oldEnd > m_lastLineStart) {
                std::string a(oldEnd - m_lastLineStart, 'A');
                m_styleBuf->replace(m_lastLineStart, oldEnd, a.c_str());
            }
        }
        // Mark the new current line 'B'.
        int lineEnd = buffer()->line_end(pos);
        if (lineEnd > len) lineEnd = len;
        m_lastLineStart = curLineStart;
        if (lineEnd > curLineStart) {
            std::string b(lineEnd - curLineStart, 'B');
            m_styleBuf->replace(curLineStart, lineEnd, b.c_str());
        }
        redraw();
    }

    int handle(int event) FL_OVERRIDE {
#if defined(_WIN32)
        // Install the in-place IME hook once the window HWND exists (focus,
        // any key press, or simply entering the widget) so pinyin composition
        // is drawn in place and the IME's floating window (its second caret)
        // stays hidden — matching the main editor. Drop any active
        // composition on unfocus.
        if (event == FL_FOCUS || event == FL_KEYBOARD || event == FL_ENTER)
            ImeInPlace::attach(this);
        else if (event == FL_UNFOCUS)
            ImeInPlace::detach(this);
#endif
        // Ctrl+Enter bubbles up to the owning window so its shortcut
        // dispatch (Window > Button > Run / Send, default Ctrl+Enter)
        // can fire. Fl_Text_Editor would otherwise insert a newline and
        // swallow the key before the window ever sees it.
        if (event == FL_KEYDOWN &&
            (Fl::event_key() == FL_Enter || Fl::event_key() == '\r') &&
            (Fl::event_state() &
             (FL_CTRL | FL_SHIFT | FL_ALT | FL_META)) == FL_CTRL)
            return 0;
        int r = Fl_Text_Editor::handle(event);
        if (m_lineHighlightOn && buffer()) {
            // Keep the style buffer in sync with the text length.
            if (m_styleBuf && m_styleBuf->length() != buffer()->length())
                syncHighlightBuffer();
            else if (!m_styleBuf && buffer()->length() > 0)
                applyHighlight();
            // Cursor moved via keyboard / mouse / selection.
            if (event == FL_KEYDOWN || event == FL_PUSH ||
                event == FL_DRAG || event == FL_RELEASE) {
                refreshHighlight();
            }
        }
        return r;
    }

    void draw() FL_OVERRIDE {
        // Composition: the buffer has no string yet, so FLTK's caret would sit
        // at compStart instead of the composition cursor. Suppress it while the
        // shared 2px composition caret is drawn below.
        const bool composing = m_compActive;
        const int savedCursorOn = mCursorOn;
        if (composing) mCursorOn = 0;
        Fl_Text_Editor::draw();
        mCursorOn = savedCursorOn;

        // Same overlay clipping as Editor.cpp: FLTK pops its text_area
        // clip before returning, so the highlight strip and the caret
        // would otherwise paint over the line-number gutter once the
        // view is scrolled horizontally.
        fl_push_clip(text_area.x, y(), text_area.w, h());

        // Current-line highlight - same two layers as the main Pecia
        // editor (Editor.cpp draw()):
        //  1. Character background via the style buffer ('B' style, set
        //     by refreshHighlight) - FLTK draws it natively, overlapping
        //     the text-selection highlight naturally.
        //  2. The line's trailing whitespace to the right edge: the style
        //     buffer cannot color empty space, so fill that strip here
        //     with a thin rect. It does NOT cover the characters.
        if (m_lineHighlightOn && buffer()) {
            int pos = insert_position();
            int blen = buffer()->length();
            if (pos < 0) pos = 0;
            if (pos > blen) pos = blen;
            int lineEnd   = buffer()->line_end(pos);
            int endX, endY;
            if (position_to_xy(lineEnd, &endX, &endY)) {
                fl_font(textfont(), textsize());
                int lineH = fl_height();
                Fl_Color hlColor = m_tc.highlight1;
                // Same geometry as Editor.cpp: from the end of the line's
                // text to the widget's right edge in WINDOW coordinates
                // (x()+w(), not w() alone - the tool editors sit at
                // x()=PAD so w() alone leaves a PAD-wide gap).
                int hlLeft = endX;
                int hlRight = x() + w();
                if (mVScrollBar && mVScrollBar->visible() &&
                    mVScrollBar->x() > hlLeft)
                    hlRight = mVScrollBar->x();
                if (hlRight > hlLeft) {
                    fl_color(hlColor);
                    fl_rectf(hlLeft, endY, hlRight - hlLeft, lineH);
                }
            }
        }

        // 光标不再自绘：构造函数已设 cursor_style(SIMPLE_CURSOR)，FLTK 自己画
        // 2px 无端帽的光标。原来那套"先用底色擦 5px 再画 3px"既遮字，又必须
        // 精确盖掉 FLTK 的原生光标（HEAVY 3px + 端帽）——擦不干净就是残影/双光标。
        // 与主编辑器 Editor 的做法保持一致。
        //
        // 但必须在这里**把 FLTK 的光标再画一遍**：上面那块「行尾到右缘」的底色
        // 是从行尾开始铺的，光标停在行尾（打字时的常态）会被它整个盖住，用户看到
        // 的就是"没有光标"。复用 FLTK 自己的 draw_cursor()，几何/宽度与它原生那次
        // 完全一致，不引入第二套几何。
        // Composition overlay: same helper, same 2px caret and same blink
        // phase as the main editor and every dialog input.
        if (composing && !m_compText.empty()) {
            int cx = 0, cy = 0;
            if (position_to_xy(m_compStart, &cx, &cy)) {
                fl_font(textfont(), textsize());
                const int lineH = fl_height();
                const bool caretVisible = mCursorOn && Fl::focus() == this;
                ImeInPlace::drawComposition(
                    m_compText.c_str(), m_compCursorBytes, cx, cy, lineH,
                    textcolor(), cursor_color(), caretVisible,
                    textfont(), textsize(), window(),
                    text_area.x, y(), text_area.w, h());
            }
        }

        if (!composing && mCursorOn && buffer() && Fl::focus() == this) {
            int selStart = 0, selEnd = 0;
            const bool hasSel = buffer()->selection_position(&selStart, &selEnd);
            const int cur = insert_position();
            if (!hasSel || cur < selStart || cur > selEnd) {
                int curX = 0, curY = 0;
                if (position_to_xy(cur, &curX, &curY)) draw_cursor(curX, curY);
            }
        }

        // End of the overlay pass: release the text_area clip again.
        fl_pop_clip();

        // Scrollbars last, on top of the highlight strip.
        drawToolScrollbar(mVScrollBar, false, m_tc);
        drawToolScrollbar(mHScrollBar, true, m_tc);
    }

private:
    const ThemeColors &m_tc;
    bool               m_lineHighlightOn;
    Fl_Text_Buffer    *m_styleBuf = nullptr;
    Fl_Text_Display::Style_Table_Entry m_styleTable[2];
    int                m_lastLineStart = -1;
    bool               m_hlDataSet = false;

    // IME composition state (same members/semantics as Editor.cpp).
    bool        m_compActive = false;
    int         m_compStart = -1;
    int         m_compEnd = -1;
    std::string m_compText;
    int         m_compCursorBytes = 0;
};
