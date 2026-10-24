#!/usr/bin/env python3
"""从 build/bench.json 生成 CODE-COMPARISON.md 用的对比表（markdown）。

用法:
    python scripts/gen_tables.py build/bench.json
"""
import json
import statistics
import sys
from collections import defaultdict

UNITS = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}
ORDER = ["scalar", "auto_sse2", "auto_native", "sse41", "avx2", "avx512"]
SIZES = {"BM_Add": (1024, 65536, 1 << 22, 1 << 25),
         "BM_Saxpy": (1024, 65536, 1 << 22, 1 << 25),
         "BM_Relu": (1024, 65536, 1 << 22, 1 << 25),
         "BM_Dot": (1024, 65536, 1 << 22, 1 << 25),
         "BM_AddF64": (1024, 1 << 16, 1 << 22),
         "BM_DotF64": (1024, 1 << 16, 1 << 22),
         "BM_Int8Dot": (4096, 1 << 16, 1 << 22),
         "BM_NormalizeSoa": (1024, 65536, 1 << 22),
         "BM_NormalizeAos": (1024, 65536, 1 << 22),
         "BM_Gather": (4096, 65536, 1 << 20)}
LABEL = {1024: "1K", 4096: "4K", 65536: "64K", 1 << 16: "64K",
         1 << 20: "1M", 1 << 22: "4M", 1 << 25: "32M", 33554432: "32M"}


def fmt(ns):
    if ns < 1000:
        return f"{ns:.1f}ns"
    if ns < 1e6:
        return f"{ns / 1e3:.1f}µs"
    return f"{ns / 1e6:.2f}ms"


def main(path):
    data = json.load(open(path, encoding="utf-8"))
    g = defaultdict(list)
    for b in data["benchmarks"]:
        if b["run_type"] != "iteration":
            continue
        p = b["name"].split("/")
        g[(p[0], "/".join(p[2:]), p[1])].append(b["real_time"] * UNITS[b["time_unit"]])
    med = {k: statistics.median(v) for k, v in g.items()}
    for kernel, sizes in SIZES.items():
        print(f"\n**{kernel}**（括号内为相对标量加速比）\n")
        cols = [v for v in ORDER if (kernel, f"n:{sizes[0]}", v) in med]
        print("| 规模 | " + " | ".join(cols) + " |")
        print("|---" * (len(cols) + 1) + "|")
        for n in sizes:
            base = med.get((kernel, f"n:{n}", "scalar"))
            cells = []
            for v in cols:
                t = med.get((kernel, f"n:{n}", v))
                if t is None:
                    cells.append("-")
                elif v == "scalar" or not base:
                    cells.append(fmt(t))
                else:
                    cells.append(f"{fmt(t)} ({base / t:.2f}×)")
            print(f"| {LABEL.get(n, n)} | " + " | ".join(cells) + " |")

    # 小规模：看拐点
    print("\n**BM_TinyAdd**（小 N，看向量化何时回本）\n")
    print("| n | scalar | auto_sse2 | auto_native | sse41 | avx2 | avx512 |")
    print("|---|---|---|---|---|---|---|")
    for n in (1, 2, 3, 7, 8, 9, 16, 17, 33):
        base = med.get(("BM_TinyAdd", f"n:{n}", "scalar"))
        cells = []
        for v in ORDER:
            t = med.get(("BM_TinyAdd", f"n:{n}", v))
            if t is None:
                cells.append("-")
            elif v == "scalar":
                cells.append(fmt(t))
            else:
                cells.append(f"{fmt(t)} ({base / t:.2f}×)")
        print(f"| {n} | " + " | ".join(cells) + " |")

    # 未对齐
    print("\n**BM_MisalignedAdd**（off = 指针偏移的 float 个数）\n")
    print("| n | off | scalar | avx2 | avx512 |")
    print("|---|---|---|---|---|")
    for n in (4096, 65536):
        for off in (0, 1, 2, 3):
            base = med.get(("BM_MisalignedAdd", f"off:{off}/n:{n}", "scalar"))
            a2 = med.get(("BM_MisalignedAdd", f"off:{off}/n:{n}", "avx2"))
            a5 = med.get(("BM_MisalignedAdd", f"off:{off}/n:{n}", "avx512"))
            print(f"| {LABEL.get(n, n)} | {off} | {fmt(base)} | {fmt(a2)} ({base / a2:.2f}×) | "
                  f"{fmt(a5)} ({base / a5:.2f}×) |")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "build/bench.json")
