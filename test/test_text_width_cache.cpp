// 文本测量缓存的门禁。
//
// 为什么需要它：这个缓存出错的**唯一表现形式是排版宽度**，不会崩溃、不会
// 报错，只是文字按错误的宽度折行。而最容易写错的地方是「键少了一维」——
// 比如只拿文本做键，换字号后整篇文档仍按旧字号的宽度排版；或者拿 FontInfo*
// 做键，字体对象被 delete_font 释放后地址复用，新字体命中旧字体的宽度。
//
// 所以这里逐个维度破坏相等：文本、face、size 任何一个不同都必须未命中。
// 这类断言刻意不去复刻 MyContainer 的实现，只断言缓存类型自身的契约。

#include "test_assert.h"
#include "mdview/text_width_cache.h"

#include <string>

static void test_miss_then_hit()
{
    pecia::TextWidthCache c;
    float w = -1.0f;
    CHECK(!c.lookup("hello", 7, 16, w));       // 空缓存必须未命中

    c.store("hello", 7, 16, 42.5f);
    CHECK(c.lookup("hello", 7, 16, w));
    CHECK_EQ(w, 42.5f);
    CHECK_EQ(c.size(), static_cast<size_t>(1));

    // 重复 store 同一个键：覆盖而不是新增条目
    c.store("hello", 7, 16, 40.0f);
    CHECK_EQ(c.size(), static_cast<size_t>(1));
    CHECK(c.lookup("hello", 7, 16, w));
    CHECK_EQ(w, 40.0f);
}

static void test_each_key_dimension_matters()
{
    pecia::TextWidthCache c;
    c.store("hello", 7, 16, 42.5f);
    float w = 0.0f;

    // 文本不同 → 未命中（否则所有词都会拿到同一个宽度）
    CHECK(!c.lookup("world", 7, 16, w));

    // face 不同 → 未命中（不同字体族同一文本宽度不同）
    CHECK(!c.lookup("hello", 8, 16, w));

    // size 不同 → 未命中。这条是最容易漏的：换字号/缩放后如果仍命中旧值，
    // 整篇文档会按前一个字号的宽度折行，而且看起来「只是有点歪」。
    CHECK(!c.lookup("hello", 7, 20, w));

    // 反向确认这三条不是因为「永远未命中」才通过的
    CHECK(c.lookup("hello", 7, 16, w));
    CHECK_EQ(w, 42.5f);
}

static void test_clear()
{
    pecia::TextWidthCache c;
    c.store("a", 1, 12, 8.0f);
    c.store("b", 1, 12, 9.0f);
    CHECK_EQ(c.size(), static_cast<size_t>(2));
    CHECK(!c.empty());

    c.clear();
    CHECK_EQ(c.size(), static_cast<size_t>(0));
    CHECK(c.empty());

    float w = 0.0f;
    CHECK(!c.lookup("a", 1, 12, w));           // 清空后必须真的失效
}

// 哈希必须把 face 和 size 分别混进去。若写成 h ^= face ^ size，
// (face=1,size=2) 与 (face=2,size=1) 会得到同一个哈希值：operator== 仍然
// 能区分（结果正确），但碰撞率无谓地翻倍，文档大时查找会退化。
static void test_hash_mixes_dimensions_separately()
{
    pecia::TextWidthCache::Hash h;
    pecia::TextWidthCache::Key a{ "x", 1, 2 };
    pecia::TextWidthCache::Key b{ "x", 2, 1 };
    CHECK(h(a) != h(b));

    // 相同输入的哈希必须稳定（unordered_map 的前提）
    CHECK_EQ(h(a), h(a));
}

int main()
{
    test_miss_then_hit();
    test_each_key_dimension_matters();
    test_clear();
    test_hash_mixes_dimensions_separately();

    int fails = test::failCount();
    printf("%d checks, %d failures\n", test::checkCount(), fails);
    return fails ? 1 : 0;
}
