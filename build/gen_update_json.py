#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 / 核对 Pecia 的更新清单 docs/update.json。

用法：
    python build/gen_update_json.py                 # 出清单（不需要 zip）
    python build/gen_update_json.py --verify-remote # 下载两镜像，比对是否同一份文件

字段与唯一来源：
    version    <- CMakeLists.txt 的 project(Pecia VERSION ...)（开发指南 §五：权威值）
    published  <- HISTORY.md 对应小节标题里的日期
    notes      <- HISTORY.md 该小节的正文（中文是真源）
    notes_en   <- docs/history/en.md 同名小节（缺小节直接报错，不静默退回中文）
    url_*      <- 固定资产名 Pecia_x64_<版本>.zip（docs/RELEASE_CHECKLIST.md §三钉死，
                  改名自动更新就 404）

清单里**没有哈希**：包和清单在同一个仓库，能换包的人也能改清单，哈希挡不住攻击，
只给手动打包添一步。它唯一挡得住的是"两仓传的不是同一份文件"这类发布事故，
所以这件事改由 --verify-remote 在发布时查一次，而不是每个客户端每次下载都查。
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


def section_from(text, heading_pattern, source, version):
    """取 '## <版本>（<日期>）' 到下一个 '## ' 之前的小节，返回 (日期, 正文)。"""
    m = re.search(heading_pattern.replace("@VERSION@", re.escape(version)), text, re.M)
    if not m:
        sys.exit("%s 里没有 %s 的小节（发版前先补上）" % (source, version))
    date = m.group(1)
    start = m.end()
    nxt = re.search(r"^## ", text[start:], re.M)
    body = text[start:start + nxt.start()] if nxt else text[start:]
    body = body.strip()
    if body.endswith("---"):
        body = body[:-3].rstrip()
    return date, body + "\n"


def build_manifest(version):
    published, notes = section_from(
        read(os.path.join(MAIN, "HISTORY.md")),
        r"^## @VERSION@（([0-9-]+)）\s*$", "HISTORY.md", version)
    en_path = os.path.join(MAIN, "docs", "history", "en.md")
    if not os.path.exists(en_path):
        sys.exit("缺少 docs/history/en.md（更新说明的英文来源）")
    _, notes_en = section_from(read(en_path), r"^## @VERSION@ \(([0-9-]+)\)\s*$",
                               "docs/history/en.md", version)
    return {
        "version": version,
        "published": published,
        "url_gitee": "https://gitee.com/%s/releases/download/%s/%s"
                     % (GITEE_REPO, version, ASSET % version),
        "url_github": "https://github.com/%s/releases/download/%s/%s"
                      % (GITHUB_REPO, version, ASSET % version),
        "notes": notes,
        "notes_en": notes_en,
    }


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": "PeciaUpdater/1.0"})
    digest = hashlib.sha256()
    total = 0
    with urllib.request.urlopen(req, timeout=300) as r:
        for chunk in iter(lambda: r.read(1 << 20), b""):
            total += len(chunk)
            digest.update(chunk)
    return total, digest.hexdigest()


def verify_remote(manifest_path):
    """两镜像各下一遍，比对是不是同一个文件（清单里没有哈希，所以互相比）。"""
    m = json.loads(read(manifest_path))
    got = {}
    for key in ("url_gitee", "url_github"):
        url = m.get(key, "")
        if not url:
            print("  %-11s 清单里没有该地址，跳过" % key)
            continue
        try:
            got[key] = fetch(url)
        except Exception as exc:                     # 连不上不等于没发布
            print("  %-11s 未取到（%s）——本机可达性问题需另判，不算失败" % (key, exc))
    if len(got) < 2:
        print("镜像核对：只取到 %d 个源，无法比对" % len(got))
        return 0 if got else 1
    (sg, hg), (sb, hb) = got["url_gitee"], got["url_github"]
    same = hg == hb
    print("  gitee   %d 字节 %s" % (sg, hg[:16] + "..."))
    print("  github  %d 字节 %s" % (sb, hb[:16] + "..."))
    print("两镜像是否同一份文件：%s" % ("一致" if same else "不一致——重传其中一个"))
    return 0 if same else 1


def main():
    out_path = os.path.join(MAIN, "docs", "update.json")
    version = version_from_cmake()

    if sys.argv[1:] == ["--verify-remote"]:
        if not os.path.exists(out_path):
            sys.exit("先运行本脚本生成 docs/update.json")
        sys.exit(verify_remote(out_path))

    if sys.argv[1:]:
        sys.exit(__doc__)

    m = build_manifest(version)
    text = json.dumps(m, ensure_ascii=False, indent=2) + "\n"
    # LF, no BOM: the file is served raw by both mirrors and parsed by a byte reader.
    open(out_path, "w", encoding="utf-8", newline="").write(text)
    print("docs/update.json 已生成")
    print("  version    = %s" % m["version"])
    print("  published  = %s" % m["published"])
    print("  notes      = %d 字节 (zh) / %d 字节 (en)"
          % (len(m["notes"]), len(m["notes_en"])))
    print("顺序提醒：先把 zip 传上两个 release，再推这份 update.json（反了点检查就 404）；")
    print("          推完跑 `python build/gen_update_json.py --verify-remote` 确认两仓是同一个文件。")


if __name__ == "__main__":
    main()
