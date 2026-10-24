#!/usr/bin/env python3
"""分析 bench_branch 的 Google Benchmark JSON：把时间换算成 cycles/元素，
按"可预测性"排序，并做几组关键对照（分支 vs CMOV、周期长度、间接分支路数）。

用法:
    python scripts/analyze.py build/bench.json
    python scripts/analyze.py build/bench.json --md > RESULTS-generated.md
"""
import argparse
import json
import re
import statistics
from collections import defaultdict

UNITS = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}
GROUP_ORDER = ["data", "store", "pattern", "corr", "density", "indirect",
               "loop", "branchless", "hint", "call", "random", "align"]


def load(path):
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    mhz = 0.0
    if data.get("context", {}).get("mhz_per_cpu"):
        mhz = float(data["context"]["mhz_per_cpu"])
    groups = defaultdict(list)
    for b in data["benchmarks"]:
        if b.get("run_type") != "iteration":
            continue
        name = b["name"]
        parts = name.split("/")
        if len(parts) < 3:
            continue
        n = 0
        label = b.get("label") or ""
        m = re.search(r"n=(\d+)", label) or re.search(r"n=(\d+)", name)
        if m:
            n = int(m.group(1))
        if n <= 0:
            continue
        groups[name].append((b["real_time"] * UNITS[b["time_unit"]], n))
    rows = []
    for name, samples in groups.items():
        times = [t for t, _ in samples]
        n = samples[0][1]
        median_ns = statistics.median(times)
        parts = name.split("/")
        rows.append(dict(name=name, group=parts[0], kernel=parts[1],
                         variant="/".join(parts[2:]), n=n,
                         ns_elem=median_ns / n,
                         cyc_elem=median_ns * mhz / 1000.0 / n,
                         cv=statistics.pstdev(times) / statistics.median(times) * 100 if len(times) > 2 else 0.0))
    return rows, mhz


def by(rows, group=None, kernel=None):
    out = [r for r in rows if (group is None or r["group"] == group) and (kernel is None or r["kernel"] == kernel)]
    return out


def find(rows, group, kernel, variant_sub):
    for r in rows:
        if r["group"] == group and r["kernel"] == kernel and variant_sub in r["variant"]:
            return r
    return None


def fmt(x, nd=3):
    return f"{x:.{nd}f}"


def section_predictability(rows):
    print("\n## 1. 数据相关分支：同一个内核，数据顺序决定可预测性（n=64K）")
    print(f"{'case':<40} {'ns/elem':>9} {'cycle/el':>9} {'vs best':>8}")
    items = by(rows, "data", "sum_if_branch")
    if not items:
        return
    best = min(r["ns_elem"] for r in items)
    for r in sorted(items, key=lambda x: -x["ns_elem"]):
        print(f"{r['variant']:<40} {fmt(r['ns_elem']):>9} {fmt(r['cyc_elem'], 2):>9} "
              f"{r['ns_elem'] / best:>7.2f}x")


def section_pattern(rows):
    print("\n## 2. 分支方向模式：周期长度 vs 预测器学习能力（pattern_sum）")
    print(f"{'pattern':<28} {'ns/elem':>9} {'cycle/el':>9} {'vs all_true':>12}")
    items = by(rows, "pattern", "pattern_sum")
    if not items:
        return
    base = next((r["ns_elem"] for r in items if r["variant"] == "pattern=all_true"), None)
    if base is None:
        return
    order = ["all_true", "all_false", "alternate", "period3", "period4", "period5", "period7",
             "period8", "period16", "period32", "period64", "period128", "period256",
             "period512", "period1024", "period2048", "period4096", "period8192",
             "period16384", "period32768", "run3", "run8", "run32", "random", "phase_switch"]
    lookup = {r["variant"].replace("pattern=", ""): r for r in items}
    for key in order:
        r = lookup.get(key)
        if not r:
            continue
        print(f"{key:<28} {fmt(r['ns_elem']):>9} {fmt(r['cyc_elem'], 2):>9} {r['ns_elem'] / base:>11.2f}x")


def section_corr(rows):
    print("\n## 3. 相关分支：内外层是否相关（nested_corr）")
    print(f"{'variant':<44} {'ns/elem':>9} {'cycle/el':>9}")
    for r in sorted(by(rows, "corr", "nested_corr"), key=lambda x: x["ns_elem"]):
        print(f"{r['variant']:<44} {fmt(r['ns_elem']):>9} {fmt(r['cyc_elem'], 2):>9}")


def section_indirect(rows):
    print("\n## 4. 间接分支：目标数 × 目标序列（switch_ways，跳转表）")
    print(f"{'case':<44} {'ns/elem':>9} {'cycle/el':>9}")
    for r in sorted(by(rows, "indirect", "switch_ways"), key=lambda x: (x["ns_elem"])):
        print(f"{r['variant']:<44} {fmt(r['ns_elem']):>9} {fmt(r['cyc_elem'], 2):>9}")


def section_branchless(rows):
    print("\n## 5. 分支 vs 无分支改写（分支版 / CMOV 版 / 查表 / 算术 / 谓词）")
    print(f"{'case':<52} {'ns/elem':>9} {'cycle/el':>9}")
    for group in ("data", "branchless", "store"):
        for r in sorted(by(rows, group), key=lambda x: (x["kernel"], x["variant"])):
            print(f"{r['name']:<52} {fmt(r['ns_elem']):>9} {fmt(r['cyc_elem'], 2):>9}")


def section_loop_call(rows):
    print("\n## 6. 循环边界与调用/返回")
    print(f"{'case':<52} {'ns/elem':>9} {'cycle/el':>9}")
    for group in ("loop", "call"):
        for r in sorted(by(rows, group), key=lambda x: x["ns_elem"]):
            print(f"{r['name']:<52} {fmt(r['ns_elem']):>9} {fmt(r['cyc_elem'], 2):>9}")


def section_hint(rows):
    print("\n## 7. 提示与布局（90%/99% 偏斜数据）")
    print(f"{'case':<44} {'ns/elem':>9} {'cycle/el':>9}")
    for r in sorted(by(rows, "hint"), key=lambda x: (x["variant"], x["kernel"])):
        print(f"{r['name']:<44} {fmt(r['ns_elem']):>9} {fmt(r['cyc_elem'], 2):>9}")


def section_mispredict_cost(rows):
    print("\n## 8. 误预测成本估算（随机 vs 可预测，同内核同 n）")
    print("   随机数据/随机模式的误预测率约 50%，可预测版本约 0%。")
    print(f"{'pair':<44} {'predictable':>12} {'random':>10} {'delta cycle/el':>15}")
    pairs = [
        ("data/sum_if_branch", "order=all_true", "order=random"),
        ("pattern/pattern_sum", "pattern=all_true", "pattern=random"),
        ("pattern/pattern_sum", "pattern=alternate", "pattern=random"),
        ("indirect/switch_ways", "ways=8,mode=seq", "ways=8,mode=random"),
    ]
    for path, a, b in pairs:
        group, kernel = path.split("/")
        ra = find(rows, group, kernel, a)
        rb = find(rows, group, kernel, b)
        if not (ra and rb):
            continue
        print(f"{path + ' ' + a + ' vs ' + b:<44} {ra['cyc_elem']:>12.2f} {rb['cyc_elem']:>10.2f} "
              f"{rb['cyc_elem'] - ra['cyc_elem']:>15.2f}")


def section_summary(rows, mhz):
    print(f"\n## 9. 汇总（{len(rows)} 个用例，CPU {mhz:.0f} MHz）")
    agg = defaultdict(list)
    for r in rows:
        agg[r["group"]].append(r)
    print(f"{'group':<12} {'cases':>6} {'min ns/el':>10} {'max ns/el':>10} {'max/min':>8}")
    for g in GROUP_ORDER:
        rs = agg.get(g)
        if not rs:
            continue
        lo = min(r["ns_elem"] for r in rs)
        hi = max(r["ns_elem"] for r in rs)
        print(f"{g:<12} {len(rs):>6} {fmt(lo):>10} {fmt(hi):>10} {hi / lo:>7.2f}x")
    cvs = [r["cv"] for r in rows if r["cv"] > 0]
    if cvs:
        print(f"\n测量稳定性：CV 中位数 {statistics.median(cvs):.2f}%，P90 "
              f"{sorted(cvs)[int(len(cvs) * 0.9)]:.2f}%，最大 {max(cvs):.2f}%")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    args = ap.parse_args()
    rows, mhz = load(args.path)
    print(f"# 数据: {args.path}  用例={len(rows)}  CPU={mhz:.0f} MHz")
    section_predictability(rows)
    section_pattern(rows)
    section_corr(rows)
    section_indirect(rows)
    section_branchless(rows)
    section_loop_call(rows)
    section_hint(rows)
    section_mispredict_cost(rows)
    section_summary(rows, mhz)


if __name__ == "__main__":
    main()
