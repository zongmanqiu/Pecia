#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 Pecia 的更新清单 docs/update.json。

用法：
    python build/gen_update_json.py        # 出清单（不需要 zip，不联网）

字段与唯一来源：
    version    <- CMakeLists.txt 的 project(Pecia VERSION ...)（开发指南 §五：权威值）
    published  <- HISTORY.md 对应小节标题里的日期
    notes      <- HISTORY.md 该小节的正文（中文是真源）
    notes_en   <- docs/history/en.md 同名小节（缺小节直接报错，不静默退回中文）
    url_*      <- 固定资产名 Pecia_x64_<版本>.zip（docs/RELEASE_CHECKLIST.md §三钉死，
                  改名自动更新就 404）

清单里**没有哈希**：包和清单在同一个仓库，能换包的人也能改清单，哈希挡不住攻击，
只给手动打包添一步。也**不比对两个镜像**：GitHub 仓是 Gitee 的自动镜像，两份内容
必然相同，为这件事写一段核对等于白下一遍包。
"""
import json
import os
import re
import sys

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


def main():
    out_path = os.path.join(MAIN, "docs", "update.json")
    version = version_from_cmake()

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
    print("          GitHub 仓是 Gitee 的自动镜像，两仓内容不需要人工比对。")


if __name__ == "__main__":
    main()
