# -*- coding: utf-8 -*-
"""Insert beginner blurbs from Docs/blurbs/*.md into the main notes file.

Blurb file format:
    ### <exact note title>
    - **🎓 初学者导读**：...

Insertion point: right after the `### ` title line and any immediately
following image lines (![...](...)) so the blurb sits before the body.
"""
import io, os, re, sys

DOCS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
MAIN = os.path.join(DOCS, "perf-book-references-notes.md")
BLURB_DIR = os.path.join(DOCS, "blurbs")

# 1. parse blurb files
blurbs = {}
for fn in sorted(os.listdir(BLURB_DIR)):
    if not fn.endswith(".md"):
        continue
    text = io.open(os.path.join(BLURB_DIR, fn), encoding="utf-8").read()
    blocks = re.split(r"(?m)^(### .+)$", text)
    # blocks = [pre, title1, body1, title2, body2, ...]
    for i in range(1, len(blocks) - 1, 2):
        title = blocks[i].strip()
        body = blocks[i + 1].strip("\n")
        if "初学者导读" not in body:
            print("WARN: no blurb body under", title[:60])
            continue
        blurbs[title] = body
print("parsed blurbs:", len(blurbs))

# 2. walk main file and insert
lines = io.open(MAIN, encoding="utf-8").read().split("\n")
out = []
inserted, missing = 0, []
i = 0
while i < len(lines):
    line = lines[i]
    out.append(line)
    if line.startswith("### "):
        title = line.strip()
        if title in blurbs:
            # skip following blank + image lines
            j = i + 1
            while j < len(lines) and (lines[j].strip() == "" or lines[j].startswith("![")):
                out.append(lines[j])
                j += 1
            # skip if already has blurb
            if j < len(lines) and "初学者导读" in lines[j]:
                i = j
                continue
            out.append("")
            out.append(blurbs[title])
            out.append("")
            inserted += 1
            i = j
            continue
        elif title not in missing:
            missing.append(title)
    i += 1

io.open(MAIN, "w", encoding="utf-8").write("\n".join(out))
print("inserted:", inserted)
if missing:
    print("titles without blurb (chapter/section headers are expected):")
    for m in missing:
        print("  -", m[:80])
