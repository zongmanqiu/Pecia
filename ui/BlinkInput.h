// BlinkInput.h - Fl_Input（及 Fl_Int_Input 等派生类）+ 与主编辑器完全相同的
// 光标闪烁与 IME 就地组合。
//
// 用法：任何需要用户输入文本的地方都用 BlinkInput / BlinkIntInput 代替
// 裸 Fl_Input / Fl_Int_Input。闪烁相位由 ui/CaretBlink.h 统一维护，周期与主编辑器
// 相同（500ms）；Fl_Input_ 自己有 2px 光标（drawtext 里 fl_rectf(...,2,height)），
// 与主编辑器的 SIMPLE_CURSOR 同宽，所以这里只需要补"闪"这一件事。
//
// IME 组合与主编辑器共用 ui/ImeInPlace 这一套实现（同一个 window-proc 钩子、
// 同一个 2px 组合光标、同一个候选窗锚点）：
//   · 组合期间隐藏 IME 的浮动组合窗（WM_IME_SETCONTEXT），不再出现"两个光标"；
//   · 组合串由 drawComposition() 就地画在插入点，拼音字母变长时不会把光标
//     甩到别处（旧实现只隐藏自己的光标、把浮动窗挪来挪去，位置由系统维护，
//     所以拼音输入时看起来会"换位置"）；
//   · 提交仍走 Fl_Input 自己的 replace()/回调，缓冲区行为不变。
#pragma once

#include "ui/CaretBlink.h"
#include "ui/ImeInPlace.h"

#include <FL/Fl.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/fl_draw.H>

#include <cstring>
#include <string>

template <class Base>
class BlinkInputT : public Base, public ImeInPlace::Host {
public:
    BlinkInputT(int X, int Y, int W, int H, const char *l = nullptr)
        : Base(X, Y, W, H, l) {
        CaretBlink::attach(this);
    }
    ~BlinkInputT() FL_OVERRIDE {
        CaretBlink::detach(this);
        ImeInPlace::release(this);
    }

    int handle(int ev) FL_OVERRIDE {
        if (ev == FL_FOCUS) {
            CaretBlink::reset();
            if (!this->readonly()) ImeInPlace::attach(this);
        } else if (ev == FL_UNFOCUS) {
            ImeInPlace::detach(this);
        } else if (ev == FL_KEYBOARD) {
            // Re-attach after the top window has been created/recreated;
            // attach() is idempotent.
            if (!this->readonly()) ImeInPlace::attach(this);
        }
        return Base::handle(ev);
    }

    void draw() FL_OVERRIDE {
        const bool composing = m_compActive && !m_compText.empty();
        // 两种情况不画 Fl_Input 自己的光标：
        //   1) 闪烁相位为"暗"，或本控件没有焦点；
        //   2) 输入法正在组词 —— 此时缓冲区里还没有组合串，Fl_Input 会按
        //      compStart 画光标，而真正的插入点在组合串内部，交给我们下面
        //      的 drawComposition() 画。
        // 做法都是把 cursor_color 临时换成底色：FLTK 自己的绘制函数照常执行，
        // 只是那一笔看不见。不做擦除，所以不会留下残影（与主编辑器同一原则）。
        const bool hide = !CaretBlink::visible() || Fl::focus() != this;
        const Fl_Color saved = this->cursor_color();
        if (hide || composing) this->cursor_color(this->color());
        Base::draw();
        this->cursor_color(saved);
        if (composing) drawCompositionOverlay();
    }

    // --- ImeInPlace::Host -------------------------------------------------
    Fl_Widget *imeWidget() override { return this; }

    void imeCompositionStart() override {
        if (this->readonly()) { m_compActive = false; return; }
        m_compActive = true;
        int p = this->insert_position();
        int m = this->mark();
        if (p > m) { int t = p; p = m; m = t; }
        m_compStart = p;
        m_compEnd = m;
        m_compText.clear();
        m_compCursorBytes = 0;
    }

    void imeCompositionUpdate(const char *utf8, int cursorBytes) override {
        if (this->readonly()) return;
        if (!m_compActive) imeCompositionStart();
        if (!m_compActive) return;
        m_compText = utf8 ? utf8 : "";
        if (cursorBytes < 0) cursorBytes = 0;
        if (cursorBytes > (int)m_compText.size())
            cursorBytes = (int)m_compText.size();
        m_compCursorBytes = cursorBytes;
        // Keep Fl_Input's own caret at the composition cursor so its
        // auto-scroll keeps the insertion point visible.
        int caret = m_compStart + m_compCursorBytes;
        if (caret > this->size()) caret = this->size();
        this->insert_position(caret);
        this->redraw();
    }

    void imeCompositionCommit(const char *utf8) override {
        if (this->readonly()) { m_compActive = false; return; }
        if (!m_compActive) imeCompositionStart();
        if (!m_compActive) return;
        const char *s = utf8 ? utf8 : "";
        int start = m_compStart;
        int end = m_compEnd;
        int len = this->size();
        if (start < 0) start = 0;
        if (start > len) start = len;
        if (end < start) end = start;
        if (end > len) end = len;
        if (start != end || *s) this->replace(start, end, s);
        this->insert_position(start + (int)strlen(s));
        m_compActive = false;
        m_compText.clear();
        m_compCursorBytes = 0;
        m_compStart = m_compEnd = -1;
        if (this->when() & FL_WHEN_CHANGED) this->do_callback(FL_REASON_CHANGED);
        this->redraw();
    }

    void imeCompositionEnd() override {
        if (!m_compActive) return;
        m_compActive = false;
        m_compText.clear();
        m_compCursorBytes = 0;
        m_compStart = m_compEnd = -1;
        this->redraw();
    }

    // Test/debug accessors (the composition is not part of value() yet).
    bool imeComposing() const { return m_compActive; }
    const std::string &imeCompositionText() const { return m_compText; }
    int imeCompositionCursor() const { return m_compCursorBytes; }
    int imeCompositionStartPos() const { return m_compStart; }

private:
    // 组合串画在 Fl_Input 的文本原点：X/Y 是去掉边框后的文本区，top 与
    // Fl_Input_::drawtext 的单行垂直居中算法一致；x 再减去水平滚动量。
    void drawCompositionOverlay() {
        const int X = this->x() + Fl::box_dx(this->box());
        const int Y = this->y() + Fl::box_dy(this->box());
        const int W = this->w() - Fl::box_dw(this->box());
        const int H = this->h() - Fl::box_dh(this->box());
        if (W <= 0 || H <= 0) return;
        fl_font(this->textfont(), this->textsize());
        const int lineH = fl_height();
        const int top = Y + (H - lineH) / 2;
        const char *val = this->value();
        int pos = m_compStart;
        int valLen = val ? (int)strlen(val) : 0;
        if (pos < 0) pos = 0;
        if (pos > valLen) pos = valLen;
        int adv = (val && pos > 0) ? (int)fl_width(val, pos) : 0;
        const int x = X - this->xscroll() + 1 + adv;
        const bool caretVisible = CaretBlink::visible() && Fl::focus() == this;
        ImeInPlace::drawComposition(
            m_compText.c_str(), m_compCursorBytes, x, top, lineH,
            this->textcolor(), this->cursor_color(), caretVisible,
            this->textfont(), this->textsize(), this->window(),
            X, Y, W, H);
    }

    bool        m_compActive = false;
    int         m_compStart = -1;
    int         m_compEnd = -1;
    std::string m_compText;
    int         m_compCursorBytes = 0;
};

using BlinkInput    = BlinkInputT<Fl_Input>;
using BlinkIntInput = BlinkInputT<Fl_Int_Input>;
