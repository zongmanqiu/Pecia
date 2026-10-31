// test_outline_collapse.cpp - 大纲折叠的可见行计算（ui/OutlineRows.cpp）。
//
// 守的是 2026-10-31 用户实测的三个 bug：
//   ① "折叠某个地方，会导致所有大纲都折叠"（兄弟共用 key）
//   ② ex1.md 里两个同名 H1「一级标题」互相连坐，而第二个挂着整篇内容
//   ③ "五级标题和六级标题左缘重合"（depth 被 min(level-1,4) 夹住）
// ①② 用"折 A 不能动到 B"把 key 的唯一性钉死，③ 用"每级缩进严格递增"钉死。
//
// 纯函数、不建窗口，所以能进 ctest 常规跑。

#include <set>
#include <string>
#include <vector>

#include "test/test_assert.h"
#include "ui/OutlineRows.h"

// CHECK_EQ 会把两边强转成整数来打印，std::string 用不了；这里补一个字符串版，
// 失败时把"实际/期望"整串打出来（折叠 bug 看可见行差异最快）。
#define CHECK_STR(actual, expected)                                            \
    do {                                                                       \
        ++test::checkCount();                                                  \
        const std::string va_ = (actual);                                       \
        const std::string vb_ = (expected);                                     \
        if (va_ != vb_) {                                                       \
            ++test::failCount();                                                \
            std::fprintf(stderr, "FAIL %s:%d: %s\n  got:  [%s]\n  want: [%s]\n",\
                         __FILE__, __LINE__, #actual, va_.c_str(), vb_.c_str());\
        }                                                                       \
    } while (0)

namespace {

struct Doc {
    std::vector<std::string> labels;
    std::vector<int>         levels;
    std::vector<std::string> ids;

    Doc(std::initializer_list<std::pair<int, const char *>> hs) {
        int i = 0;
        for (const auto &h : hs) {
            labels.push_back(h.second);
            levels.push_back(h.first);
            ids.push_back("toc-" + std::to_string(i++));
        }
    }

    std::vector<SideRow> rows(const std::set<std::string> &collapsed) const {
        return outline_rows::build(labels, levels, ids, collapsed);
    }
};

// 可见行文本，'|' 分隔：断言"哪些行还在"比逐行比对更抗噪。
std::string visible(const std::vector<SideRow> &rows) {
    std::string s;
    for (const auto &r : rows) {
        if (!s.empty()) s += "|";
        s += r.label;
    }
    return s;
}

const SideRow *find(const std::vector<SideRow> &rows, const std::string &label) {
    for (const auto &r : rows) {
        if (r.label == label) return &r;
    }
    return nullptr;
}

// 取某行的折叠 key。测试**不自己拼 key**（拼法属于实现细节，且带同名序号）；
// 从"全展开"那次构建里读回来，折叠时再喂回去，这样 key 规则怎么改都不断。
// 同名标题取第 nth 个（0 起）。
std::string keyOfNth(const Doc &doc, const std::string &label, int nth) {
    for (const auto &r : doc.rows({})) {
        if (r.label != label) continue;
        if (nth-- == 0) return r.key;
    }
    return std::string("<missing>");
}

std::string keyOf(const Doc &doc, const std::string &label) {
    return keyOfNth(doc, label, 0);
}

// A
//   A1
//     A1a
//   A2
// B
//   B1
// C
const Doc kDoc{ { 1, "A" }, { 2, "A1" }, { 3, "A1a" }, { 2, "A2" },
                { 1, "B" }, { 2, "B1" }, { 1, "C" } };

void test_no_collapse() {
    const auto rows = kDoc.rows({});
    CHECK_STR(visible(rows), std::string("A|A1|A1a|A2|B|B1|C"));
    // 有子标题的才画箭头（有箭头才谈得上折叠）
    CHECK(find(rows, "A") && find(rows, "A")->hasChildren);
    CHECK(find(rows, "A1") && find(rows, "A1")->hasChildren);
    CHECK(find(rows, "A2") && !find(rows, "A2")->hasChildren);
    CHECK(find(rows, "C") && !find(rows, "C")->hasChildren);
    // 默认全展开
    for (const auto &r : rows)
        if (r.hasChildren) CHECK(r.expanded);
    // 每行的 key 必须唯一：这条一破，折叠就会连坐（两次线上 bug 都是它）
    std::set<std::string> keys;
    for (const auto &r : rows) keys.insert(r.key);
    CHECK_EQ(keys.size(), rows.size());
}

void test_collapse_top_level() {
    const auto rows = kDoc.rows({ keyOf(kDoc, "A") });
    CHECK_STR(visible(rows), std::string("A|B|B1|C"));
    // 被折叠的那行自己必须留着，否则箭头没了就再也展开不回来
    CHECK(find(rows, "A") && !find(rows, "A")->expanded);
}

void test_collapse_middle_keeps_siblings() {
    // ★ bug①的核心断言：折叠 A1 只能带走 A1a，不能动 A2 / B1。
    const auto rows = kDoc.rows({ keyOf(kDoc, "A1") });
    CHECK_STR(visible(rows), std::string("A|A1|A2|B|B1|C"));
    CHECK(find(rows, "A1") && !find(rows, "A1")->expanded);
    CHECK(find(rows, "A") && find(rows, "A")->expanded);   // 祖先不受影响
}

void test_collapse_sibling_with_children() {
    // S
    //   X
    //     X1
    //   Y
    //     Y1
    // 折 X 之后 Y1 必须还在
    const Doc doc{ { 1, "S" }, { 2, "X" }, { 3, "X1" },
                   { 2, "Y" }, { 3, "Y1" } };
    const auto rows = doc.rows({ keyOfNth(doc, "X", 0) });
    CHECK_STR(visible(rows), std::string("S|X|Y|Y1"));
    CHECK(find(rows, "X") && !find(rows, "X")->expanded);
    CHECK(find(rows, "Y") && find(rows, "Y")->expanded);
}

void test_nested_collapse_and_restore() {
    const auto both = kDoc.rows({ keyOf(kDoc, "A"), keyOf(kDoc, "B") });
    CHECK_STR(visible(both), std::string("A|B|C"));

    // 只折 B，A 的子树完整保留
    const auto onlyB = kDoc.rows({ keyOf(kDoc, "B") });
    CHECK_STR(visible(onlyB), std::string("A|A1|A1a|A2|B|C"));

    // 折 A 的同时折 A1：恢复 A 时 A1 仍是折叠态（折叠集合是独立的）
    const std::set<std::string> s{ keyOf(kDoc, "A"), keyOf(kDoc, "A1") };
    CHECK_STR(visible(kDoc.rows(s)), std::string("A|B|B1|C"));
    CHECK_STR(visible(kDoc.rows({ keyOf(kDoc, "A1") })),
              std::string("A|A1|A2|B|B1|C"));
}

// ★ bug②：同名标题（ex1.md 里两个 H1「一级标题」）不能互相连坐。
//   第二个「一级标题」下面挂着整篇内容，一旦共用 key，折第一个等于折全篇。
void test_duplicate_labels_do_not_share_key() {
    const Doc doc{
        { 1, "文档标题" }, { 2, "简介" },
        { 1, "一级标题" }, { 2, "二级标题" }, { 3, "三级标题" },   // 第一个
        { 1, "一级标题" }, { 2, "2. 段落" }, { 2, "3. 文本样式" },  // 第二个
    };
    CHECK_STR(visible(doc.rows({})),
              std::string("文档标题|简介|一级标题|二级标题|三级标题|"
                          "一级标题|2. 段落|3. 文本样式"));

    const std::string first = keyOfNth(doc, "一级标题", 0);
    const std::string second = keyOfNth(doc, "一级标题", 1);
    CHECK(first != second);                       // 同名不同 key
    CHECK(first.find("<missing>") == std::string::npos);

    // 折第一个：只带走"二级标题/三级标题"，第二个「一级标题」和它的小节必须还在
    const auto rows = doc.rows({ first });
    CHECK_STR(visible(rows),
              std::string("文档标题|简介|一级标题|一级标题|2. 段落|3. 文本样式"));
    CHECK(find(rows, "一级标题") && !find(rows, "一级标题")->expanded);
    CHECK(find(rows, "2. 段落") && find(rows, "3. 文本样式"));

    // 折第二个：反过来只带走它自己的小节
    const auto rows2 = doc.rows({ second });
    CHECK_STR(visible(rows2),
              std::string("文档标题|简介|一级标题|二级标题|三级标题|一级标题"));
}

void test_skip_level() {
    // H1 直接跟 H3：跳级也算"祖先/后代"，不能按"恰好 +1"判
    const Doc doc{ { 1, "T" }, { 3, "deep" }, { 4, "deeper" }, { 2, "back" } };
    CHECK_STR(visible(doc.rows({})), std::string("T|deep|deeper|back"));
    const auto rows = doc.rows({ keyOf(doc, "deep") });
    CHECK_STR(visible(rows), std::string("T|deep|back"));
    CHECK(find(rows, "T") && find(rows, "T")->hasChildren);   // 跳级仍是子标题
    CHECK(find(rows, "back") && find(rows, "back")->hasChildren == false);
}

// ★ bug③：五级/六级标题左缘重合（2026-10-31 用户实测）。
//
//   depth 曾用 std::min(level-1, 4) 封顶，H5 和 H6 双双被夹到 4，两级文字
//   左对齐。封顶只该挡非法级别，不该挡合法的 H6 —— 而"每深一级就多缩进一格"
//   正是这层数据唯一对外承诺的东西，所以直接把它钉成断言。
void test_indent_depth_monotonic() {
    const Doc doc{ { 1, "h1" }, { 2, "h2" }, { 3, "h3" },
                   { 4, "h4" }, { 5, "h5" }, { 6, "h6" } };
    const auto rows = doc.rows({});
    CHECK_EQ(rows.size(), (size_t)6);

    // 绝对深度：H1 = 0 … H6 = 5（SidePanel 用 depth * 14px 算缩进）
    for (int lv = 1; lv <= 6 && (size_t)lv <= rows.size(); ++lv)
        CHECK_EQ(rows[(size_t)lv - 1].depth, lv - 1);

    // 相对关系：后一级必须比前一级更深（严格递增，不能相等）
    for (size_t i = 1; i < rows.size(); ++i) {
        CHECK(rows[i].depth > rows[i - 1].depth);
        // 且相邻两级只差一级（跳级文档里也成立：h5→h6 只差 1）
        CHECK_EQ(rows[i].depth - rows[i - 1].depth, 1);
    }
}

void test_empty_document() {
    // 没有标题时返回空表：合成"无标题"行要查 I18n，由调用方补
    const std::vector<std::string> noLabels;
    const auto rows = outline_rows::build(noLabels, {}, {}, {});
    CHECK(rows.empty());
}

void test_missing_arrays_tolerated() {
    // levels / ids 比 labels 短（预览路径不给行号、极端情况给空数组）不能越界
    const std::vector<std::string> labels{ "only" };
    const auto rows = outline_rows::build(labels, {}, {}, {});
    CHECK_EQ(rows.size(), (size_t)1);
    if (rows.size() == 1) {
        CHECK_EQ(rows[0].level, 1);      // 缺省按 H1
        CHECK(rows[0].anchor.empty());
        CHECK(!rows[0].hasChildren);
    }
}

}  // namespace

int main() {
    test_no_collapse();
    test_collapse_top_level();
    test_collapse_middle_keeps_siblings();
    test_collapse_sibling_with_children();
    test_nested_collapse_and_restore();
    test_duplicate_labels_do_not_share_key();
    test_skip_level();
    test_indent_depth_monotonic();
    test_empty_document();
    test_missing_arrays_tolerated();

    const int fails = test::failCount();
    std::fprintf(stderr, "%d checks, %d failures\n", test::checkCount(), fails);
    return fails ? 1 : 0;
}
