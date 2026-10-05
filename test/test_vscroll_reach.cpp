// test_vscroll_reach.cpp — 门禁：拖到最底时滑块必须真的抵达轨道末端
//
// ★ 这个门禁守的是**用户诉求本身**：
//   "把滑块拖到底，滑块必须抵达轨道末端，不能总差一点。"
//
// ★ 背景（2026-10-05 定位结论，已用离屏探针 + 用户截图交叉验证）
//   FLTK 的 Fl_Text_Display 里"能滚到哪"和"滑块量程到哪"由两处独立算出：
//     scroll_()            上限 = mNBufferLines + 3 - mNVisibleLines
//     update_v_scrollbar() 量的maximum = mNBufferLines + 2 - mNVisibleLines
//   并且 display_needs_recalc() :710-713 还有一个 while 循环，在"倒数第二行
//   是空的"时反复 scroll_(mTopLineNum-1) 把视图往上拉。文档末尾必然有空行，
//   所以真实可达的 top 比名义上限还低 1~3 行。
//
//   滑块画在 xx = int(val*(L-S)+0.5)，val = (top-min)/(max-min)。
//   top 永远到不了 max -> val 到不了 1.0 -> 滑块恒差 (L-S)/(max-min) 像素。
//   实测：有状态栏 5px、无状态栏 2px、更矮窗口 6px（不是常数，故"有时候"）。
//
// ★ 修法（改 FLTK，见 main/patches/fltk-1.4.5/src/Fl_Text_Display.cxx）
//   把"能滚到哪"和"滑块量程到哪"统一到同一个常量
//   vscroll_bottom_line()，并让 mNVisibleLines 只数**完整放得下**的行（floor）。
//   同时末尾留 PECIA_BLANK_ROWS_PAST_END 行空白，display_needs_recalc() 里
//   的回拉循环到达底部即停止。
//
// ★ 为什么本测试用真实 Fl_Text_Display 而不是复刻公式
//   项目里犯过的错：断言落在实现细节上，实现一改测试就跟着错。
//   这里直接实例化 Fl_Text_Display、真的滚到底、再按产品用的绘制公式
//   (ui/ThemeWidgets.h::scrollbarThumbGeometry) 算滑块位置——
//   FLTK 那侧的量程计算无法在测试里复刻，必须让真实控件说话。

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/Enumerations.H>
#include <FL/fl_attr.h>

#include "test_assert.h"
#include "ui/ThemeWidgets.h"

#include <cstdio>
#include <string>

// patched FLTK 的 Fl_Menu 需要这个符号（与 test_vscroll_geom.cpp 同理）
Fl_Color g_popupCheckColor = 0;

// 暴露 FLTK 的 protected 计数器，并提供"拖到底"的动作。
class Probe : public Fl_Text_Display {
public:
    Probe(int X, int Y, int W, int H) : Fl_Text_Display(X, Y, W, H) {}
    Fl_Scrollbar* vsb() { return mVScrollBar; }
    int taH() const { return text_area.h; }
    int nVis() const { return mNVisibleLines; }
    int nBuf() const { return mNBufferLines; }
    int top() const { return mTopLineNum; }
    int mxs() const { return mMaxsize; }
    // 该显示行是否超出文档末尾（FLTK 自己维护 mLineStarts，可信）
    bool rowBlank(int r) const {
        if (!mLineStarts || r < 0 || r >= mNVisibleLines) return true;
        return mLineStarts[r] == -1;
    }
    void settle() { resize(x(), y(), w(), h()); for (int i = 0; i < 6; i++) Fl::check(); }

    // 用户把滑块拖到底。
    //
    // ★ 必须走 dragThumbToTrackEnd()（真实滑块映射那条路），不能用
    //   scroll(巨大值) 直连：后者绕过 Fl_Slider 的量程映射，测不出
    //   "用户往下拖到底却到不了"这一类回归。项目里已经因此让门禁
    //   假绿过两次。
    void dragToBottom() { dragThumbToTrackEnd(); }

    // ★ 走**真实拖动路径**把滑块拉到最底。
    //
    // 这点至关重要：Fl_Slider::handle() 的 FL_DRAG 分支按
    //     v = round(xx*(max-min)/(ww-S) + min)
    // 把鼠标位置映射到 [min, max]（Fl_Slider.cxx:282）。也就是说
    // **用户能到达的位置被 maximum 钳死** —— 若maximum 被错误地压到当前
    // 顶行，用户往下拖多少都动不了，文档只剩第一屏可见。
    // 直接调scroll() 会绕过这个映射 hence 测不出该回归。
    //
    // 这里模拟：在轨道上按下并逐步下移到轨道末端，每步都让 FLTK 用
    // 真实的 handle() 逻辑算值，最后再让 display 同步。
    void dragThumbToTrackEnd() {
        Fl_Scrollbar *sb = mVScrollBar;
        if (!sb) return;
        const int L = sb->h();
        const int W = sb->w();
        for (int step = 0; step <= 40; ++step) {
            // 手指/鼠标逐步下移；终点是轨道最底端
            const int my = (L * step) / 40;
            // 复刻 Fl_Slider::handle() 的映射：越界会被钳到 max
            int S = (int)(sb->slider_size() * L + 0.5f);
            const int T = W / 2 + 1;
            if (S < T) S = T;
            if (S >= L) S = L;
            int xx = my;
            if (xx > L - S) xx = L - S;
            if (xx < 0) xx = 0;
            double v = 0;
            if (L - S > 0)
                v = (double)((long long)((double)xx *
                          (sb->maximum() - sb->minimum()) / (L - S) + 0.5))
                    + sb->minimum();
            if (v < sb->minimum()) v = sb->minimum();
            if (v > sb->maximum()) v = sb->maximum();
            sb->value((int)v);     // Fl_Scrollbar::value(int) 隐藏了基类的
                               // value(double)：这里从编译第一天起走的就是
                               // int 版。显式写 (int) 与历史行为一致，消除 C4244。
            scroll((int)v, 0);
            settle();
        }

        // ★ 最后：把滑块真正压到轨道最底端，正如用户把鼠标拖出轨道下沿。
        //   这一步不可省略。前面每一步settle() 都会触发一轮
        //   display_needs_recalc()，而 wrap 模式下每次重算都会重新统计
        //   mNBufferLines（文档末尾的空行只有视图滚到那里才被计入），于是
        //   maximum 会随着一步步下移而抬高。前面 40 步用的都是**当时**的
        //   maximum 算出来的值，最后一步必须以**此刻**的 maximum 为准，
        //   否则量程被抬高后就再也回不到 val=1。
        //   真实拖动同样如此：用户把手拖到轨道下沿时产生的是一次新的
        //   FL_DRAG 事件，用的是当场的 maximum。
        {
            const int L2 = sb->h();
            const int W2 = sb->w();
            int S2 = (int)(sb->slider_size() * L2 + 0.5f);
            const int T2 = W2 / 2 + 1;
            if (S2 < T2) S2 = T2;
            if (S2 >= L2) S2 = L2;
            int xx = L2 - S2;                  // 轨道最底
            if (xx < 0) xx = 0;
            double v = sb->minimum();
            if (L2 - S2 > 0)
                v = (double)((long long)((double)xx *
                          (sb->maximum() - sb->minimum()) / (L2 - S2) + 0.5))
                    + sb->minimum();
            if (v < sb->minimum()) v = sb->minimum();
            if (v > sb->maximum()) v = sb->maximum();
            sb->value((int)v);
            scroll((int)v, 0);
            settle();
        }
    }
};

// 按产品绘制公式算滑块，返回"滑块末端到轨道末端的间隙"
static int thumbGap(Fl_Scrollbar *sb) {
    int len = 0, pos = 0;
    scrollbarThumbGeometry(sb, 0, sb->h(), sb->w(), &len, &pos);
    return (sb->h() - len) - pos;      // == (L-S) - xx
}

static int thumbPos(Fl_Scrollbar *sb) {
    int len = 0, pos = 0;
    scrollbarThumbGeometry(sb, 0, sb->h(), sb->w(), &len, &pos);
    return pos;
}

// 自生成输入。过去这里读仓库根的 qw.md —— 一个从未进 git 的用户私有文件，
// 它一消失（2026-10-05 实锤）整条门禁就红，而且红灯看起来像"滚动逻辑坏了"。
// 门禁的输入必须由门禁自己造（与 test_preview_gate 的 makeScratch 同一惯例）。
//
// 行数取 400：最大窗高 1000px / 行高 18px ≈ 56 行可见，拖动契约要求 top
// 走 100+ 行，400 行留足余量。行长 ~52 列 ASCII，800px 宽 / Courier 16
// （每字符约 9.6px → 一行可容 ~83 列）下不折行，物理行数 = 逻辑行数，可预测。
static Fl_Text_Buffer *makeBuffer() {
    static const int kLines = 400;
    std::string s;
    s.reserve((size_t)kLines * 64);
    for (int i = 1; i <= kLines; ++i) {
        char row[80];
        std::snprintf(row, sizeof(row),
                      "line %03d: the quick brown fox jumps over the lazy dog\n", i);
        s += row;
    }
    Fl_Text_Buffer *buf = new Fl_Text_Buffer();
    buf->text(s.c_str());
    return buf;
}

// ★ 契约 1：拖到底时滑块必须**正好**抵达轨道末端，间隙为 0。
//   覆盖多种窗口高度—— 间隙随轨道长度变化，必须都成立。
static void test_thumb_reaches_track_end() {
    static const int heights[] = {400, 600, 624, 800, 1000};
    Fl_Text_Buffer *buf = makeBuffer();
    CHECK(buf->length() > 0);

    for (int hi = 0; hi < (int)(sizeof(heights) / sizeof(heights[0])); ++hi) {
        const int winH = heights[hi];
        Fl_Double_Window *win = new Fl_Double_Window(800, winH);
        Probe *d = new Probe(0, 0, 800, winH);
        d->textfont(FL_COURIER);
        d->textsize(16);
        d->buffer(buf);
        d->wrap_mode(Probe::WRAP_AT_BOUNDS, 0);
        d->scrollbar_size(10);
        win->begin(); d->show(); win->end(); win->show();
        d->resize(0, 0, 800, winH);
        for (int i = 0; i < 20; i++) Fl::check();

        // 环境自检：布局没跑过的话 text_area 为 0，下面的断言全是假结论
        if (d->taH() <= 0) {
            std::fprintf(stderr, "环境无效 winH=%d\n", winH);
            delete d; win->end(); delete win;
            continue;
        }

        Fl_Scrollbar *sb = d->vsb();
        d->dragToBottom();
        const int gap = thumbGap(sb);
        std::fprintf(stderr, "  winH=%4d taH=%d nVis=%d top=%d max=%.0f gap=%d\n",
                     winH, d->taH(), d->nVis(), d->top(), sb->maximum(), gap);
        // ★ 零间隙
        CHECK(gap == 0);

        win->end();
        delete win;
    }
}

// ★ 契约 2：反复拖到底必须稳定（不漂移、不累积误差）。
//   修法把maximum 封顶到 top，若实现有累积误差，多轮后间隙会变。
static void test_repeated_drag_is_stable() {
    Fl_Text_Buffer *buf = makeBuffer();
    Fl_Double_Window *win = new Fl_Double_Window(800, 600);
    Probe *d = new Probe(0, 0, 800, 600);
    d->textfont(FL_COURIER);
    d->textsize(16);
    d->buffer(buf);
    d->wrap_mode(Probe::WRAP_AT_BOUNDS, 0);
    d->scrollbar_size(10);
    win->begin(); d->show(); win->end(); win->show();
    d->resize(0, 0, 800, 600);
    for (int i = 0; i < 20; i++) Fl::check();

    if (d->taH() > 0) {
        Fl_Scrollbar *sb = d->vsb();
        int worst = 0;
        for (int i = 0; i < 8; ++i) {
            d->dragToBottom();
            const int g = thumbGap(sb);
            if (g > worst) worst = g;
        }
        std::fprintf(stderr, "  8 轮拖到底，最大间隙=%d\n", worst);
        CHECK(worst == 0);
    }
    win->end();
    delete win;
}

// ★ 契约 3：到达顶端时滑块贴在轨道起点（对称性；只修底端不算修好）
static void test_thumb_at_top() {
    Fl_Text_Buffer *buf = makeBuffer();
    Fl_Double_Window *win = new Fl_Double_Window(800, 600);
    Probe *d = new Probe(0, 0, 800, 600);
    d->textfont(FL_COURIER);
    d->textsize(16);
    d->buffer(buf);
    d->wrap_mode(Probe::WRAP_AT_BOUNDS, 0);
    d->scrollbar_size(10);
    win->begin(); d->show(); win->end(); win->show();
    d->resize(0, 0, 800, 600);
    for (int i = 0; i < 20; i++) Fl::check();

    if (d->taH() > 0) {
        Fl_Scrollbar *sb = d->vsb();
        d->scroll(1, 0);
        d->settle();
        CHECK(thumbPos(sb) == 0);
    }
    win->end();
    delete win;
}

// ★ 契约 4：走**真实拖动路径**时，必须能一路拖到底。
//   若 maximum 被错误地压到当前顶行，Fl_Slider 的拖动映射
//   v = xx*(max-min)/(L-S)+min 会把值钳死在原地，文档只剩第一屏可见。
//   这条曾被漏掉并造成真实回归（用户报"四百多行只显示二十多行"），
//   所以必须用拖动路径验证，且必须验证"逐步推进"，不能只看终点。
static void test_scrolling_still_works() {
    Fl_Text_Buffer *buf = makeBuffer();
    Fl_Double_Window *win = new Fl_Double_Window(800, 600);
    Probe *d = new Probe(0, 0, 800, 600);
    d->textfont(FL_COURIER);
    d->textsize(16);
    d->buffer(buf);
    d->wrap_mode(Probe::WRAP_AT_BOUNDS, 0);
    d->scrollbar_size(10);
    win->begin(); d->show(); win->end(); win->show();
    d->resize(0, 0, 800, 600);
    for (int i = 0; i < 20; i++) Fl::check();

    if (d->taH() > 0) {
        Fl_Scrollbar *sb = d->vsb();

        // 4a) 从顶部开始，沿轨道逐步下移，顶行必须持续推进
        d->scroll(1, 0); d->settle();
        const int startTop = d->top();
        int prev = startTop;
        int firstStuck = -1;
        const int realLines = 479;
        // 目标：至少能推进到"末行完整可见"的位置（nBuf-nVis+1 附近）
        for (int step = 1; step <= 40 && firstStuck < 0; ++step) {
            const int L = sb->h();
            int S = (int)(sb->slider_size() * L + 0.5f);
            const int T = sb->w() / 2 + 1;
            if (S < T) S = T;
            if (S >= L) S = L;
            const int xx = ((L - S) * step) / 40;
            double v = sb->minimum();
            if (L - S > 0)
                v = (double)xx * (sb->maximum() - sb->minimum()) / (L - S)
                    + sb->minimum();
            if (v > sb->maximum()) v = sb->maximum();
            sb->value((int)v);
            d->scroll((int)v, 0);
            d->settle();
            if (d->top() <= prev && d->top() < realLines - d->nVis()) {
                firstStuck = step;
            }
            prev = d->top();
        }
        std::fprintf(stderr, "  拖动推进：start=%d末 top=%d sbMax=%.0f 卡住步=%d\n",
                     startTop, d->top(), sb->maximum(), firstStuck);
        // ★ 中途不得卡住（这就是那个回归的表现）
        CHECK(firstStuck < 0);

        // 4b) 拖到轨道末端后，顶行必须明显超过起点（真的滚下去了）
        d->dragThumbToTrackEnd();
        std::fprintf(stderr, "  拖到轨道末端：top=%d（应远大于 %d）\n",
                     d->top(), startTop);
        CHECK(d->top() > startTop + 100);
    }
    win->end();
    delete win;
}

// ★ 契约 5：短文档（放得下）不应被误改—— 此时不该出现滚动条量程异常
static void test_short_document_unaffected() {
    Fl_Text_Buffer *buf = new Fl_Text_Buffer();
    buf->text("line1\nline2\nline3\n");
    Fl_Double_Window *win = new Fl_Double_Window(800, 600);
    Probe *d = new Probe(0, 0, 800, 600);
    d->textfont(FL_COURIER);
    d->textsize(16);
    d->buffer(buf);
    d->wrap_mode(Probe::WRAP_AT_BOUNDS, 0);
    d->scrollbar_size(10);
    win->begin(); d->show(); win->end(); win->show();
    d->resize(0, 0, 800, 600);
    for (int i = 0; i < 20; i++) Fl::check();

    if (d->taH() > 0) {
        d->scroll(1, 0);
        d->settle();
        CHECK(d->top() == 1);           // 放得下就该停在顶
        Fl_Scrollbar *sb = d->vsb();
        CHECK(thumbGap(sb) >= 0);       // 不产生负间隙（滑块不越界）
    }
    win->end();
    delete win;
}

// ★ 契约 6：拖到最底时，可见行必须**完整放得下**，不能有半行被切掉。
//
//   背景：上游用 ceil 算可见行数
//       nvlines = (text_area.h + mMaxsize - 1) / mMaxsize
//   也就是"能塞几行就算几行"，可最后一行要 mMaxsize 像素而实际只剩
//   text_area.h mod mMaxsize 像素。每行画在 Y = text_area.y + row*mMaxsize
//   且整体被裁剪到 text_area（draw() 里的 fl_clip_box），于是那多算出来
//   的一行底部被切掉。滚到底时文档最后一行正好落在这行上，于是被腰斩。
//
//   实测（qw.md / Consolas 16 / wrap 开）：
//       编辑器 h=545 -> text_area.h=539, mMaxsize=18
//       ceil  -> nVis=30 需 540px，只有 539 -> 底行被裁 1px  ★有字
//       floor -> nVis=29 需 522px-> 全部完整，余 17px
//   h=600 时 594=33×18 恰好整除，所以旧版本"有时候"不复现。
//
//   契约就是这行：nVis*mMaxsize <= text_area.h。
static void test_bottom_row_not_clipped() {
    static const int heights[] = {400, 500, 545, 600, 624, 700, 800, 1000};
    Fl_Text_Buffer *buf = makeBuffer();
    CHECK(buf->length() > 0);

    for (int hi = 0; hi < (int)(sizeof(heights) / sizeof(heights[0])); ++hi) {
        const int winH = heights[hi];
        Fl_Double_Window *win = new Fl_Double_Window(800, winH);
        Probe *d = new Probe(0, 0, 800, winH);
        d->textfont(FL_COURIER);
        d->textsize(16);
        d->buffer(buf);
        d->wrap_mode(Probe::WRAP_AT_BOUNDS, 0);
        d->scrollbar_size(10);
        win->begin(); d->show(); win->end(); win->show();
        d->resize(0, 0, 800, winH);
        for (int i = 0; i < 20; i++) Fl::check();

        if (d->taH() <= 0) {
            std::fprintf(stderr, "环境无效 winH=%d\n", winH);
            delete d; win->end(); delete win;
            continue;
        }

        const int over = d->nVis() * d->mxs() - d->taH();
        std::fprintf(stderr, "  裁剪 winH=%4d taH=%4d nVis=%3d mMaxsize=%d 超出=%d\n",
                     winH, d->taH(), d->nVis(), d->mxs(), over);
        // ★ 可见行必须全部完整
        CHECK(over <= 0);

        win->end();
        delete win;
    }
}

// ★ 契约 7：拖到最底时，文档最后一行下面必须留有空白行。
//
//   这是所有主流编辑器的通行做法：末行不贴着窗口底边。其意义不只是好看：
//   末行留白之后，它的下边框不再紧贴视口底边，也就不会和状态栏/边框挤在
//   一起；同时继续按方向键会出现自然的"越过头"手感。
//
//   实现上靠 PECIA_BLANK_ROWS_PAST_END（见 FLTK 补丁）以及 display_needs_recalc()
//   里那段"回拉"循环在到达底部时停止回拉。
static void test_blank_rows_past_end() {
    static const int heights[] = {545, 600, 700};
    Fl_Text_Buffer *buf = makeBuffer();
    CHECK(buf->length() > 0);

    for (int hi = 0; hi < (int)(sizeof(heights) / sizeof(heights[0])); ++hi) {
        const int winH = heights[hi];
        Fl_Double_Window *win = new Fl_Double_Window(800, winH);
        Probe *d = new Probe(0, 0, 800, winH);
        d->textfont(FL_COURIER);
        d->textsize(16);
        d->buffer(buf);
        d->wrap_mode(Probe::WRAP_AT_BOUNDS, 0);
        d->scrollbar_size(10);
        win->begin(); d->show(); win->end(); win->show();
        d->resize(0, 0, 800, winH);
        for (int i = 0; i < 20; i++) Fl::check();

        if (d->taH() <= 0) {
            delete d; win->end(); delete win;
            continue;
        }

        // 走真实拖动路径到底，然后立刻读数（不能再调scroll()，那会触发
        // 第二次 recalc 并把 mNBufferLines 抬高，读到的是中间态）
        d->dragThumbToTrackEnd();

        int blanks = 0;
        for (int r = d->nVis() - 1; r >= 0; --r) {
            if (d->rowBlank(r)) blanks++; else break;
        }
        std::fprintf(stderr, "  空白 winH=%4d nVis=%3d top=%d 末尾空白行=%d\n",
                     winH, d->nVis(), d->top(), blanks);
        // ★ 至少留 1 行空白（实测目标 3 行；只要 >0 即达成用户诉求）
        CHECK(blanks >= 1);

        win->end();
        delete win;
    }
}

int main() {
    Fl::visual(FL_RGB);
    test_thumb_reaches_track_end();
    test_repeated_drag_is_stable();
    test_thumb_at_top();
    test_scrolling_still_works();
    test_short_document_unaffected();
    test_bottom_row_not_clipped();
    test_blank_rows_past_end();
    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n",
                 test::checkCount(), fails);
    return fails ? 1 : 0;
}
