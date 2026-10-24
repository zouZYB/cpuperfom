#!/usr/bin/env python3
"""分析 bench_simd 的 Google Benchmark JSON：把 ns 换算成 cycles/元素、GB/s、SIMD 效率。

用法:
    python scripts/analyze.py build/bench.json --ghz 5.02 > build/analysis.txt
"""
import argparse
import json
import re
import statistics
from collections import defaultdict

# 最小访存字节/元素（读 + 写）与含 write-allocate（RFO）的等效访存字节
TRAFFIC = {
    "BM_Add": 12, "BM_Saxpy": 12, "BM_Relu": 8, "BM_Dot": 8,
    "BM_Gather": 8, "BM_NormalizeSoa": 24, "BM_NormalizeAos": 24,
    "BM_Int8Dot": 2, "BM_AddF64": 24, "BM_DotF64": 16,
    "BM_MisalignedAdd": 12, "BM_TinyAdd": 12,
}
TRAFFIC_RFO = {
    "BM_Add": 16, "BM_Saxpy": 12, "BM_Relu": 12, "BM_Dot": 8,
    "BM_Gather": 8, "BM_NormalizeSoa": 24, "BM_NormalizeAos": 24,
    "BM_Int8Dot": 2, "BM_AddF64": 32, "BM_DotF64": 16,
    "BM_MisalignedAdd": 16, "BM_TinyAdd": 16,
}
UNITS = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}
WIDTH = {"scalar": 1, "sse41": 4, "avx2": 8, "avx512": 16}
SIZE_LABEL = {1: "1", 2: "2", 3: "3", 7: "7", 8: "8", 9: "9", 16: "16", 17: "17",
              33: "33", 1024: "1K", 4096: "4K", 65536: "64K", 1 << 16: "64K",
              1 << 20: "1M", 1 << 22: "4M", 1 << 25: "32M"}
TIER = {1024: "L1", 4096: "L1", 65536: "L2", 1 << 16: "L2", 1 << 20: "L2/L3",
        1 << 22: "L3", 1 << 25: "DRAM"}


def parse_args(s):
    n = None
    off = None
    m = re.search(r"n:(\d+)", s)
    if m:
        n = int(m.group(1))
    m = re.search(r"off:(\d+)", s)
    if m:
        off = int(m.group(1))
    return off, n


def load(path):
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    rows = []
    for b in data["benchmarks"]:
        if b["run_type"] != "iteration":
            continue
        parts = b["name"].split("/")
        if len(parts) < 3:
            continue
        kernel, variant = parts[0], parts[1]
        argstr = "/".join(parts[2:])
        off, n = parse_args(argstr)
        rows.append(dict(kernel=kernel, variant=variant, off=off, n=n,
                         ns=b["real_time"] * UNITS[b["time_unit"]]))
    return rows


def medians(rows):
    groups = defaultdict(list)
    for r in rows:
        groups[(r["kernel"], r["off"], r["n"], r["variant"])].append(r["ns"])
    return {k: statistics.median(v) for k, v in groups.items()}, groups


def get(med, kernel, n, variant, off=None):
    for (k, o, nn, v), t in med.items():
        if k == kernel and v == variant and nn == n and (off is None or o == off):
            return t
    return None


def section_sizes(med, ghz, kernel):
    if kernel in ("BM_TinyAdd", "BM_MisalignedAdd"):
        return
    print(f"\n## {kernel}  ({TRAFFIC[kernel]} B/elem 最小访存)")
    print(f"{'size':>6} {'层次':>5} {'scalar(ns)':>11} {'scalar cyc/el':>13} "
          f"{'最佳':>12} {'加速':>7} {'cyc/el':>8} {'GB/s':>7} {'B/cycle':>8}")
    sizes = sorted({k[2] for k in med if k[0] == kernel and k[1] is None})
    for n in sizes:
        base = get(med, kernel, n, "scalar")
        if base is None:
            continue
        best = min(((get(med, kernel, n, v), v) for v in ("auto_sse2", "auto_native", "sse41", "avx2", "avx512")
                    if get(med, kernel, n, v) is not None), default=(None, None))
        if best[0] is None:
            continue
        t, v = best
        gbs = TRAFFIC[kernel] * n / t
        print(f"{SIZE_LABEL.get(n, n):>6} {TIER.get(n, '?'):>5} {base:11.1f} {base * ghz / n:13.2f} "
              f"{v:>12} {base / t:6.2f}x {t * ghz / n:8.2f} {gbs:7.1f} {gbs / ghz:8.1f}")


def section_misaligned(med, ghz):
    print("\n## 未对齐效应（BM_MisalignedAdd，偏移单位=float）")
    print(f"{'n':>6} {'off':>4} {'avx2 加速':>10} {'avx2 ns':>10} {'avx512 加速':>12} {'avx512 ns':>11}")
    for n in (4096, 65536):
        for off in (0, 1, 2, 3):
            a2 = get(med, "BM_MisalignedAdd", n, "avx2", off)
            a5 = get(med, "BM_MisalignedAdd", n, "avx512", off)
            s = get(med, "BM_MisalignedAdd", n, "scalar", off)
            if not (a2 and a5 and s):
                continue
            print(f"{SIZE_LABEL.get(n, n):>6} {off:>4} {s / a2:9.2f}x {a2:10.1f} {s / a5:11.2f}x {a5:11.1f}")


def section_tiny(med, ghz):
    print("\n## 小规模开销（BM_TinyAdd，avx2）")
    print(f"{'n':>4} {'scalar(ns)':>11} {'avx2(ns)':>9} {'加速':>7} {'每元素 ns':>10}")
    for n in (1, 2, 3, 7, 8, 9, 16, 17, 33):
        s = get(med, "BM_TinyAdd", n, "scalar")
        a = get(med, "BM_TinyAdd", n, "avx2")
        if s and a:
            print(f"{n:>4} {s:11.2f} {a:9.2f} {s / a:6.2f}x {a / n:10.3f}")


def section_efficiency(med):
    print("\n## SIMD 效率 = 加速比 / 理论宽度（>100% 说明基线是延迟受限而非吞吐受限）")
    print(f"{'kernel':<17} {'size':>6} {'sse41(4)':>9} {'avx2(8)':>9} {'avx512(16)':>11}")
    for kernel in ("BM_Add", "BM_Saxpy", "BM_Relu", "BM_Dot", "BM_DotF64",
                   "BM_Int8Dot", "BM_NormalizeSoa", "BM_NormalizeAos"):
        for n in (1024, 65536, 1 << 22):
            base = get(med, kernel, n, "scalar")
            if not base:
                continue
            cells = []
            for v in ("sse41", "avx2", "avx512"):
                t = get(med, kernel, n, v)
                cells.append(f"{100 * base / t / WIDTH[v]:.0f}%" if t else "-")
            print(f"{kernel:<17} {SIZE_LABEL[n]:>6} {cells[0]:>9} {cells[1]:>9} {cells[2]:>11}")


def section_stability(groups):
    print("\n## 测量稳定性（每用例 3 次重复）")
    cvs, worst = [], []
    for k, vals in groups.items():
        if len(vals) < 2:
            continue
        m = statistics.median(vals)
        if m <= 0:
            continue
        cv = statistics.pstdev(vals) / m * 100
        cvs.append(cv)
        worst.append((cv, k, vals))
    cvs.sort()
    worst.sort(reverse=True)
    print(f"用例={len(cvs)}  CV 中位数={statistics.median(cvs):.2f}%  "
          f"P90={cvs[int(len(cvs) * 0.9)]:.2f}%  最大={cvs[-1]:.2f}%")
    print("CV 最大的 6 个用例:")
    for cv, k, vals in worst[:6]:
        print(f"  {cv:5.1f}%  {k[0]}/{k[3]}/n={k[2]}  " + ", ".join(f"{v:.1f}" for v in vals))


def section_decay(med, ghz):
    print("\n## 规模衰减：add 的每级内存层次（avx2 与标量对照）")
    print(f"{'n':>6} {'层次':>5} {'scalar(cyc/el)':>15} {'avx2(cyc/el)':>13} "
          f"{'GB/s(RFO)':>10} {'B/cycle':>8}")
    for n in (1024, 65536, 1 << 22, 1 << 25):
        s = get(med, "BM_Add", n, "scalar")
        a = get(med, "BM_Add", n, "avx2")
        if not (s and a):
            continue
        gbs = TRAFFIC_RFO["BM_Add"] * n / a
        print(f"{SIZE_LABEL[n]:>6} {TIER[n]:>5} {s * ghz / n:15.2f} {a * ghz / n:13.2f} "
              f"{gbs:10.1f} {gbs / ghz:8.1f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--ghz", type=float, default=5.02)
    args = ap.parse_args()
    rows = load(args.path)
    med, groups = medians(rows)
    print(f"# 数据: {args.path}  用例={len(med)}  实测频率={args.ghz:.2f} GHz")
    for kernel in ("BM_Add", "BM_Saxpy", "BM_Relu", "BM_Dot", "BM_Int8Dot",
                   "BM_NormalizeSoa", "BM_NormalizeAos", "BM_Gather",
                   "BM_AddF64", "BM_DotF64"):
        section_sizes(med, args.ghz, kernel)
    section_misaligned(med, args.ghz)
    section_tiny(med, args.ghz)
    section_efficiency(med)
    section_decay(med, args.ghz)
    section_stability(groups)


if __name__ == "__main__":
    main()
