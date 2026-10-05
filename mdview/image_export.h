#pragma once
#include <string>

// 远程图片取回回调：把 url 的原始字节写到 dest，成功返回 true。
// 缺省实现 default_remote_fetch() 用 WinHTTP 同步 GET（不弹窗，内置 3 次重试
// 与 2xx 状态码校验）。测试注入假实现，避免门禁依赖真实网络。
using RemoteFetchFn = bool (*)(const std::string& url, const std::string& dest);

RemoteFetchFn default_remote_fetch();

// URL → 本地文件名（沿用URL 里的原始文件名与真实扩展名，如
// "https://x/result.png" → "result.png"）。URL 本身没有可用名字时退回
// "image_<hash><ext>"。哈希前缀 "image_" 极少出现，仅作兜底。
//
// 预览容器下载远程图与导出时的本地化**必须用同一个函数**，否则两边命名
// 不一致，导出的目录里会出现"同一张图两个名字"，甚至互相覆盖。
// 返回空串表示这个 URL 推不出可靠的文件名（调用方应保持原样，别猜）。
std::string remote_image_file_name(const std::string& url);

// 把 html 里的远程 <img src> 改写为 build_dir 内的本地相对文件。
// fetch == nullptr：只复用 build_dir 里已有的文件，绝不触网 —— 用于渲染
// 路径（预览必须快，容器正在后台下载那些图）。
// fetch != nullptr：缺失的图当场下载后再改写 —— 用于"在浏览器打开"与导出，
// 因为渲染后台线程写出 index.html 时远程图往往还在下载，那份 HTML 是"图还
// 没到"的快照，而下载完成后的重绘不会重写 index.html。
// 未取到 / 无可靠文件名 → 原样保留远程 URL（不做恶化）。
//
// 产物目录里因此只有**一份**图：既供 litehtml 预览解码，也是浏览器/导出
// 直接可用的资源。不再有独立的 temp\*.img 缓存副本。
void localize_remote_images(std::string& html, const std::string& build_dir,
                            RemoteFetchFn fetch = nullptr);

// 数一数 html 里还剩几个远程（http/https）<img src>。本地化之后这个数就是
// "页面里仍然依赖网络、离线打开会缺图"的图片数量，导出后据此如实告知用户。
int count_remote_image_srcs(const std::string& html);
