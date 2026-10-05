// test_vscroll_geom.cpp — 门禁：滚动条滑块"绘制矩形"必须与"命中矩形"重合
//
// ★ 这个门禁守的是一条**契约**，不是实现细节：
//   用户看到的滑块 == 用户能抓到的滑块。
//   一旦两者错开，Fl_Slider::handle() 就会把 offcenter 钳到 S，
//   拖到轨道底时被钳到 (L - S - (S-1)) 而不是 (L - S)，
//   取到的 v < maximum —— 即用户报的"垂直滑块滑不到底，总是有一点小间隙"。
//
// 契约的权威来源是 FLTK 自己，两处必须一致：
//   绘制/命中尺寸 S = max(int(slider_size*L + .5), (horizontal?H:W)/2 + 1)
//   起点xx = int(val * (L - S) + .5)
// 见 Fl_Slider.cxx:130-140（draw）与 :256-259（handle）。
//
// ★ 本测试**不复刻**被测代码的公式（那是项目里已犯过的错：断言落在实现上，
//   实现一改进测试就跟着错，伪装成"测试坏了"）。它改为：
//   1) 用 FLTK 头文件里公开的 API 构造真实 Fl_Scrollbar；
//   2) 直接调用被测的 scrollbarThumbGeometry()；
//   3) 断言"输出的矩形"落在"FLTK 认可的矩形"之内 —— 即按契约判定，
//      不重算生产代码的算式。
//   另有一条独立的"旧公式会失败"对照，见 test_old_formula_would_fail。

#include <FL/Fl.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/Enumerations.H>
#include <FL/fl_attr.h>

#include "test_assert.h"
#include "ui/ThemeWidgets.h"

#include <cmath>
#include <cstdio>

// patched FLTK 的 Fl_Menu 需要这个符号；产品里由 core/Theme.cpp 定义，
// 本测试不链 Theme.cpp，故自带一份（与 test/test_stubs.cpp 同理）。
Fl_Color g_popupCheckColor = 0;

// ★ Fl_Scrollbar declares `int value(int p)` (Fl_Scrollbar.H:71), which **hides**
//   the inherited `Fl_Valuator::value(double)`. So `sb.value(some_double)`
//   silently narrows to int (warning C4244) and truncates the requested
//   position. Expose an unambiguous setter on a tiny subclass instead.
class TestScrollbar : public Fl_Scrollbar {
public:
    TestScrollbar(int X, int Y, int W, int H) : Fl_Scrollbar(X, Y, W, H) {}
    void setValueDouble(double v) { Fl_Valuator::value(v); }
};

// FLTK 的权威命中几何 —— 直接照抄 Fl_Slider.cxx 的两条公式作为"契约参照"。
// 这是**独立于被测代码**的第二实现，用来做交叉校验。
static void fltkReferenceGeometry(const Fl_Scrollbar &sb,
                                  int trackLen, int crossSize,
                                  int *S_out, int *xx_out) {
    const int ww = trackLen;
    int S = (int)(sb.slider_size() * ww + .5);
    const int T = crossSize / 2 + 1;
    if (S < T) S = T;
    double val;
    if (sb.minimum() == sb.maximum()) {
        val = 0.5;
    } else {
        val = (sb.value() - sb.minimum()) / (sb.maximum() - sb.minimum());
        if (val > 1.0) val = 1.0;
        else if (val < 0.0) val = 0.0;
    }
    *S_out = S;
    *xx_out = (int)(val * (ww - S) + .5);
}

// 模拟：按下位置 grab（相对轨道起点）后拖到轨道最底，最终得到的 value
// —— 逐行照抄 Fl_Slider::handle() 的 FL_PUSH + FL_DRAG 分支
static double dragToBottom(const Fl_Scrollbar &sb, int S, int xx0, int grab) {
    const int L = sb.h();                    // 垂直滚动条：轨道长 = h
    int offcenter = grab - xx0;
    if (offcenter < 0) offcenter = 0;
    else if (offcenter > S) offcenter = S;

    const int drag_mx = L - 1;
    int xx = drag_mx - offcenter;
    if (xx < 0) { xx = 0; offcenter = drag_mx; if (offcenter < 0) offcenter = 0; }
    else if (xx > (L - S)) {
        xx = L - S;
        offcenter = drag_mx - xx;
        if (offcenter > S) offcenter = S;
    }
    return std::round(xx * (sb.maximum() - sb.minimum()) / (double)(L - S)
                      + sb.minimum());
}

// ---- 场景表：覆盖长/短文档、多个窗口高度、多个字号 ----
struct Scenario {
    const char *name;
    int    track;        // 轨道长
    int    cross;        // 滚动条宽（10px）
    double slider_size;  // FLTK 算出的可见比例
    double minv, maxv;
};

static const Scenario kScenarios[] = {
    // 长文档：slider_size 很小 ->旧公式的20px 下限最容易造成错开
    { "long_doc_2000L",396, 10, 0.0164835, 1, 1970 },
    { "long_doc_8000L",396, 10, 0.00287428, 1, 7980 },
    { "long_doc_h700",  686, 10, 0.00512372, 1, 7962 },
    { "mid_doc_478L",   596, 10, 0.06875,1, 448 },
    { "short_doc_50L",  386, 10, 0.442308,  1, 30 },
    { "short_doc_12L",  394, 10, 0.96,      1, 3 },
    { "ratio_one",386, 10, 1.0,           1, 100 },
    { "ratio_zero",     386, 10, 0.0,           1, 100 },
};

static const int kNumScenarios = (int)(sizeof(kScenarios) / sizeof(kScenarios[0]));

// 三个代表位置：25% / 50% / 75%
static const double kFractions[3] = { 0.25, 0.50, 0.75 };

static void test_draw_matches_hit_region() {
    for (int i = 0; i < kNumScenarios; ++i) {
        const Scenario &sc = kScenarios[i];
        for (int fi = 0; fi < 3; ++fi) {
            const double v = sc.minv + (sc.maxv - sc.minv) * kFractions[fi];

            TestScrollbar sb(0, 0, sc.cross, sc.track);
            sb.slider_size((float)sc.slider_size);
            sb.bounds(sc.minv, sc.maxv);
            sb.setValueDouble(v);

            // 被测实现
            int thumb_len = 0, thumb_pos = 0;
            scrollbarThumbGeometry(&sb, 0 /*trackOrigin*/, sc.track,
                                   sc.cross, &thumb_len, &thumb_pos);

            // 契约参照（独立第二实现）
            int refS = 0, refXx = 0;
            fltkReferenceGeometry(sb, sc.track, sc.cross, &refS, &refXx);

            // 契约：绘制矩形必须与 FLTK 命中矩形一致
            CHECK(thumb_len == refS);
            CHECK(thumb_pos == refXx);

            // 契约：滑块不得越出轨道（否则末行会有"拖过头"的负间隙）
            CHECK(thumb_pos >= 0);
            CHECK(thumb_pos + thumb_len <= sc.track);

            // FLTK 在 S >= L 时直接放弃拖拽（Fl_Slider.cxx:256
            // `if (S >= ww) return 0;`），此时 L-S == 0，映射退化。
            // 那里不存在"拖不到底"的问题，故跳过拖拽契约。
            if (refS >= sc.track) continue;

            // 契约：可见滑块的**每一个可点行**都必须能拖到 maximum。
            // 这是用户诉求的直接表达（"滑块能滑到最后"）。
            const double maxv = sb.maximum();
            for (int r = 0; r < thumb_len; ++r) {
                const int grab = thumb_pos + r;
                if (grab < 0 || grab >= sc.track) continue;
                const double got = dragToBottom(sb, refS, refXx, grab);
                CHECK(got >= maxv - 0.5);
            }
        }
    }
}

// 水平方向同样成立（工具窗/横向滚动条用得到）
static void test_horizontal_matches_hit_region() {
    const int track = 596, cross = 10;
    TestScrollbar sb(0, 0, track, cross);
    sb.slider_size(0.05f);
    sb.bounds(1, 900);
    sb.value(450);

    int thumb_len = 0, thumb_pos = 0;
    scrollbarThumbGeometry(&sb, 0, track, cross, &thumb_len, &thumb_pos);

    int refS = 0, refXx = 0;
    fltkReferenceGeometry(sb, track, cross, &refS, &refXx);
    CHECK(thumb_len == refS);
    CHECK(thumb_pos == refXx);
    CHECK(thumb_pos >= 0);
    CHECK(thumb_pos + thumb_len <= track);
}

// 对照组：证明"旧的 20px 下限公式"确实违反契约。
// 这条不是测产品代码，而是把历史 bug 钉成可执行的证据。
static void test_old_formula_would_fail() {
    const Scenario &sc = kScenarios[0];       // long_doc_2000L
    TestScrollbar sb(0, 0, sc.cross, sc.track);
    sb.slider_size((float)sc.slider_size);
    sb.bounds(sc.minv, sc.maxv);
    sb.setValueDouble(sc.minv + (sc.maxv - sc.minv) * 0.5);

    int refS = 0, refXx = 0;
    fltkReferenceGeometry(sb, sc.track, sc.cross, &refS, &refXx);

    // 旧公式：thumb = max(int(L*sl), 20)，位置 = int(val*(L-thumb))
    const int oldLen = (int)(sc.track * sc.slider_size) < 20
                     ? 20 : (int)(sc.track * sc.slider_size);
    const double val = (sb.value() - sb.minimum())
                     / (sb.maximum() - sb.minimum());
    const int oldPos = (int)(val * (sc.track - oldLen));

    // 旧公式画出来的滑块明显比命中区大 => 存在"看得见却抓不住"的死区
    CHECK(oldLen != refS);                       // 两者确实不同
    CHECK(oldPos + oldLen > refXx + refS);        // 旧绘制区超出命中区

    // 并且：在旧绘制区内确实存在拖不到底的死区
    int deadRows = 0;
    for (int r = 0; r < oldLen; ++r) {
        const int grab = oldPos + r;
        if (grab < 0 || grab >= sc.track) continue;
        const int oc = grab - refXx;
        const bool inThumb = (oc >= 0 && oc <= refS - 1);
        const double got = dragToBottom(sb, refS, refXx, grab);
        if (!inThumb || got < sb.maximum() - 0.5) ++deadRows;
    }
    CHECK(deadRows > 0);// 死区确实存在（历史 bug 的可执行证据）
}

// 边界：极小轨道不应溢出或产生负长度
static void test_degenerate_track() {
    TestScrollbar sb(0, 0, 10, 1);       // 轨道长 1px
    sb.slider_size(0.5f);
    sb.bounds(1, 1000);
    sb.value(500);

    int thumb_len = 0, thumb_pos = 0;
    scrollbarThumbGeometry(&sb, 0, 1, 10, &thumb_len, &thumb_pos);
    CHECK(thumb_len >= 0);
    CHECK(thumb_len <= 1);
    CHECK(thumb_pos >= 0);
    CHECK(thumb_pos + thumb_len <= 1);
}

int main() {
    Fl::visual(FL_RGB);
    test_draw_matches_hit_region();
    test_horizontal_matches_hit_region();
    test_old_formula_would_fail();
    test_degenerate_track();
    int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n", test::checkCount(), fails);
    return fails ? 1 : 0;
}