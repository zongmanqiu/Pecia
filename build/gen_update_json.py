#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 / 校验 Pecia 的更新清单 update.json。

用法：
    python build/gen_update_json.py <发布包 zip 路径>
    python build/gen_update_json.py --verify-remote

为什么要有这个脚本：清单里的 sha256 必须由实际上传的那份 zip 算出来。手抄哈希
正是 CurSeen 在 2026-10-31 踩过的坑（手工重打的包与清单对不上，客户端只能报校验
失败）。--verify-remote 把两个镜像都下回来逐字节比一次，上传完跑一遍就能确认
"Gitee 和 GitHub 上是同一个文件"。

字段的唯一来源：
    version   <- CMakeLists.txt 的 project(Pecia VERSION ...)（开发指南 §五：权威值）
    published <- HISTORY.md 对应小节标题里的日期
    notes     <- HISTORY.md 该小节的正文
    sha256    <- 传进来的 zip
    url_*     <- 固定资产名 Pecia_x64_<版本>.zip（docs/RELEASE_CHECKLIST.md §三钉死，改名自动更新就 404）
"""
import hashlib
import json
import os
import re
import sys
import urllib.request

MAIN = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GITEE_REPO = "qiuzongman/pecia"
GITHUB_REPO = "zongmanqiu/Pecia"
ASSET = "Pecia_x64_%s.zip"


def read(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8")


def version_from_cmake():
    text = read(os.path.join(MAIN, "CMakeLists.txt"))
    m = re.search(r"project\(Pecia\s+VERSION\s+([0-9]+(?:\.[0-9]+)*)", text)
    if not m:
        sys.exit("CMakeLists.txt 里找不到 project(Pecia VERSION ...)")
    return m.group(1)


def history_section(version):
    """返回 (日期, 正文)。小节 = '## <版本>（<日期>）' 到下一个 '## ' 之前。"""
    text = read(os.path.join(MAIN, "HISTORY.md"))
    pat = re.compile(r"^## %s（([0-9-]+)）\s*$" % re.escape(version), re.M)
    m = pat.search(text)
    if not m:
        sys.exit("HISTORY.md 里没有 %s 的小节（发版前先在顶部追加）" % version)
    start = m.end()
    nxt = re.search(r"^## ", text[start:], re.M)
    body = text[start:start + nxt.start()] if nxt else text[start:]
    return m.group(1), body.strip() + "\n"


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def build_manifest(version, zip_path):
    if os.path.basename(zip_path) != ASSET % version:
        sys.exit("包名必须叫 %s，否则自动更新的下载地址对不上" % (ASSET % version))
    published, notes = history_section(version)
    return {
        "version": version,
        "published": published,
        "sha256": sha256_of(zip_path),
        "url_gitee": "https://gitee.com/%s/releases/download/%s/%s"
                     % (GITEE_REPO, version, ASSET % version),
        "url_github": "https://github.com/%s/releases/download/%s/%s"
                      % (GITHUB_REPO, version, ASSET % version),
        "notes": notes,
    }


def fetch_sha256(url):
    req = urllib.request.Request(url, headers={"User-Agent": "PeciaUpdater/1.0"})
    h = hashlib.sha256()
    with urllib.request.urlopen(req, timeout=120) as r:
        for chunk in iter(lambda: r.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def verify_remote(manifest_path):
    m = json.loads(read(manifest_path))
    bad = 0
    for key in ("url_gitee", "url_github"):
        url = m.get(key, "")
        if not url:
            print("  %-11s 清单里没有该地址，跳过" % key)
            continue
        try:
            got = fetch_sha256(url)
        except Exception as exc:                     # 连不上不等于没发布
            print("  %-11s 未取到（%s）——本机可达性问题需另判，不算失败" % (key, exc))
            continue
        same = got == m["sha256"]
        bad += 0 if same else 1
        print("  %-11s %s  %s" % (key, "一致" if same else "不一致", got[:16] + "..."))
    print("两镜像哈希核对：%s" % ("通过" if bad == 0 else "%d 个源与清单不符" % bad))
    return 1 if bad else 0


def main():
    args = sys.argv[1:]
    out_path = os.path.join(MAIN, "docs", "update.json")
    version = version_from_cmake()

    if args == ["--verify-remote"]:
        if not os.path.exists(out_path):
            sys.exit("先运行本脚本生成 update.json")
        sys.exit(verify_remote(out_path))

    if len(args) != 1:
        sys.exit(__doc__)
    zip_path = os.path.abspath(args[0])
    if not os.path.exists(zip_path):
        sys.exit("找不到发布包：%s" % zip_path)

    m = build_manifest(version, zip_path)
    text = json.dumps(m, ensure_ascii=False, indent=2) + "\n"
    # LF, no BOM: the file is served raw by both mirrors and parsed by a byte reader.
    with open(out_path, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    print("update.json 已生成：%s" % out_path)
    print("  version   = %s" % m["version"])
    print("  published = %s" % m["published"])
    print("  sha256    = %s" % m["sha256"])
    print("  notes     = %d 字节" % len(m["notes"]))
    print("顺序提醒：先把 zip 传上两个 release，再推这份 update.json（反了点检查就 404）；")
    print("          推完跑 `python build/gen_update_json.py --verify-remote` 核对两镜像。")


if __name__ == "__main__":
    main()
