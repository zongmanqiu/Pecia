#pragma once
#ifndef PECIA_TEXT_WIDTH_CACHE_H
#define PECIA_TEXT_WIDTH_CACHE_H

#include <string>
#include <unordered_map>

namespace pecia {

// 文本测量缓存：把 (文本, 字体 face, 字体 size) 映射到测量出的像素宽度。
//
// 为什么要独立成类型而不是直接塞在 MyContainer 里：
//   这类缓存的典型故障**不会崩溃**，只会让文字按错误的宽度排版（换字号后
//   命中旧宽度、不同字体族互相串味），肉眼很难第一时间归因。抽出来之后它
//   只依赖标准库，可以用纯单测把「键的每个维度都必须参与比较」钉死。
//
// 键里为什么必须有 face 和 size：
//   litehtml 每次 create_font 返回的是一个 FontInfo*，delete_font 之后那块
//   内存会被下一个字体复用 —— 用指针做键，就可能在换字号/换字重后命中前一个
//   字体的宽度。face 与 size 才是真正决定 fl_width 结果的输入。
class TextWidthCache {
public:
    struct Key {
        std::string text;
        int face = 0;
        int size = 0;

        bool operator==(const Key& o) const
        {
            return face == o.face && size == o.size && text == o.text;
        }
    };

    struct Hash {
        size_t operator()(const Key& k) const
        {
            size_t h = std::hash<std::string>()(k.text);
            // 两个 int 分别混合。直接 h ^= face ^ size 会让
            // (face=1,size=2) 与 (face=2,size=1) 落到同一个桶里，
            // 虽然 operator== 仍能区分（不会错），但碰撞率会无谓地高。
            h ^= std::hash<int>()(k.face) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(k.size) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            return h;
        }
    };

    // 命中返回 true 并写出宽度；未命中返回 false，调用方负责真正测量后 store。
    bool lookup(const std::string& text, int face, int size, float& out) const
    {
        auto it = m_map.find(Key{ text, face, size });
        if (it == m_map.end()) return false;
        out = it->second;
        return true;
    }

    void store(const std::string& text, int face, int size, float width)
    {
        m_map[Key{ text, face, size }] = width;
    }

    void clear() { m_map.clear(); }
    size_t size() const { return m_map.size(); }
    bool empty() const { return m_map.empty(); }

private:
    std::unordered_map<Key, float, Hash> m_map;
};

}  // namespace pecia

#endif  // PECIA_TEXT_WIDTH_CACHE_H
