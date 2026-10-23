// CaretBlink.h - 全进程统一的输入光标闪烁（500ms，与主编辑器一致）
//
// 为什么需要它：FLTK 的 Fl_Input **自己不会闪** —— Fl_Input_::drawtext() 里
// 只有"有焦点就画光标"这一条判断，没有定时器（源码里那句注释 "they don't blink"
// 说的就是这个）。于是主编辑器有 500ms 闪烁、而对话框里的搜索框/参数框光标是
// 死的，两套观感。这里用一个进程级的 500ms 定时器维护相位，输入类控件在
// draw() 里查 visible() 决定这一帧画不画光标。
//
// 与主编辑器是**两个独立定时器**，但周期相同（500ms）。相位不需要跨窗口对齐：
// 任何时刻只有一个窗口有焦点，也只有那个窗口会画光标。
#pragma once

#include <FL/Fl.H>
#include <FL/Fl_Widget.H>

#include <cstddef>
#include <vector>

namespace CaretBlink {

// 函数内静态变量在 inline 函数里是"全程序唯一"的，所以这个头文件不需要配套
// .cpp，也不许改造成普通的全局变量（那样每个 TU 一份，闪相位会各走各的）。
inline bool &phase() {
    static bool on = true;
    return on;
}

inline std::vector<Fl_Widget *> &registry() {
    static std::vector<Fl_Widget *> v;
    return v;
}

inline bool &started() {
    static bool started_ = false;
    return started_;
}

inline void onTick(void *) {
    phase() = !phase();
    for (Fl_Widget *w : registry())
        if (w && w->visible_r() && Fl::focus() == w) w->redraw();
    Fl::repeat_timeout(0.5, onTick);
}

// 当前相位：true = 显示光标
inline bool visible() { return phase(); }

// 获得焦点时立刻亮起，否则点进输入框最多要等 500ms 才看得见光标。
inline void reset() { phase() = true; }

inline void attach(Fl_Widget *w) {
    if (!w) return;
    auto &v = registry();
    for (Fl_Widget *x : v)
        if (x == w) return;
    v.push_back(w);
    if (!started()) {
        started() = true;
        Fl::add_timeout(0.5, onTick);
    }
}

inline void detach(Fl_Widget *w) {
    auto &v = registry();
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == w) {
            v.erase(v.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

} // namespace CaretBlink
