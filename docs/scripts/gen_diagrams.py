# -*- coding: utf-8 -*-
"""Generate SVG study diagrams for perf-book reference notes. Stdlib only."""
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from svglib import SVG

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "images")
os.makedirs(OUT, exist_ok=True)

BLUE, GREEN, ORANGE, RED, PURPLE, GRAY = "#3b6ea5", "#27ae60", "#e67e22", "#c0392b", "#8e44ad", "#7f8c8d"
LBLUE, LGREEN, LORANGE, LRED, LPURPLE, LGRAY = "#e8f0fa", "#e9f7ef", "#fdf2e3", "#fdedec", "#f4ecf7", "#f2f4f4"


# ---------------------------------------------------------------- 1. TMA tree
def tma_tree():
    s = SVG(920, 560)
    s.text(460, 30, "TMA 自顶向下微架构分析：把所有流水线槽位分层归因", 18, bold=True)

    # L0
    s.rect(310, 50, 300, 50, fill=LGRAY, stroke=GRAY)
    s.multiline(460, 70, ["Pipeline Slots（发射槽位）", "≈ 每周期 4 槽 × 核周期数"], 13)

    # L1 boxes
    l1 = [
        (30,  LGREEN, GREEN,  "Retiring 退役", "真正完成有用功", "越高越好"),
        (255, LRED,   RED,    "Bad Speculation", "误预测/投机浪费", "分支误预测"),
        (480, LORANGE, ORANGE,"Front-End Bound", "前端供给不足", "取指/译码停顿"),
        (705, LPURPLE, PURPLE,"Back-End Bound", "后端资源不足", "等数据/等执行口"),
    ]
    for x, f, st, t1, t2, t3 in l1:
        s.arrow(460, 100, x + 92, 135, stroke=st)
        s.rect(x, 135, 185, 64, fill=f, stroke=st)
        s.multiline(x + 92, 156, [t1, t2, t3], 12)

    # L2 under Front-End
    fe2 = [(480, "Fetch Latency", "I-Cache/ITLB miss、分支重定向"),
           (660, "Fetch Bandwidth", "译码宽度限制 MITE/DSB 切换")]
    for x, t1, t2 in fe2:
        s.arrow(572, 199, x + 85, 235, stroke=ORANGE)
        s.rect(x, 235, 170, 58, fill="#fef9f0", stroke=ORANGE, sw=1.2)
        s.multiline(x + 85, 255, [t1, t2], 11)

    # L2 under Back-End
    be2 = [(705, "Memory Bound", "等内存数据（各级 cache miss）"),
           (480, "Core Bound", "端口争用/除法器/依赖链")]
    s.arrow(797, 199, 790, 315, stroke=PURPLE)
    s.rect(705, 315, 190, 58, fill="#f9f2fb", stroke=PURPLE, sw=1.2)
    s.multiline(800, 335, ["Memory Bound", "等内存数据（cache miss）"], 11)
    s.arrow(760, 199, 565, 315, stroke=PURPLE)
    s.rect(480, 315, 190, 58, fill="#f9f2fb", stroke=PURPLE, sw=1.2)
    s.multiline(575, 335, ["Core Bound", "端口争用/长延迟运算"], 11)

    # L3 under Memory Bound
    mem3 = ["L1 Bound", "L2 Bound", "L3/LLC Bound", "DRAM Bound"]
    for i, t in enumerate(mem3):
        x = 620 + (i % 2) * 145
        y = 400 + (i // 2) * 56
        s.rect(x, y, 135, 44, fill="#ffffff", stroke=PURPLE, sw=1.0, rx=6)
        s.text(x + 67, y + 27, t, 11)
    s.arrow(800, 373, 800, 396, stroke=PURPLE)

    # 使用说明
    s.rect(30, 400, 540, 140, fill="#fbfcfe", stroke=BLUE, sw=1.2)
    s.multiline(50, 425, [
        "怎么用：",
        "1. 先跑 perf stat -M TopdownL1 看四个大桶占比",
        "2. 哪个桶占比高，就沿树向下钻取（L2/L3 指标）",
        "3. 定位到叶节点后，查 perf-book 对应优化章节",
        "实战命令：perf stat -M TopdownL1 ./app",
    ], 12, anchor="start")
    s.save(os.path.join(OUT, "tma-tree.svg"))


# ---------------------------------------------------------------- 2. CPU pipeline
def cpu_pipeline():
    s = SVG(980, 470)
    s.text(490, 28, "现代乱序超标量 CPU 流水线：前端供给 μop，后端乱序执行", 18, bold=True)

    # Front end
    s.rect(20, 60, 150, 90, fill=LBLUE, stroke=BLUE)
    s.multiline(95, 84, ["取指 Fetch", "I-Cache + 分支预测", "16B/周期"], 12)
    s.rect(190, 60, 190, 90, fill=LBLUE, stroke=BLUE)
    s.multiline(285, 80, ["译码供给 3 条路径", "MITE：传统译码器", "DSB：μop Cache", "LSD：循环缓冲"], 11)
    s.rect(400, 60, 140, 90, fill=LORANGE, stroke=ORANGE)
    s.multiline(470, 84, ["分配/重命名", "Register Rename", "写入 ROB"], 12)

    # ROB
    s.rect(560, 60, 120, 90, fill=LRED, stroke=RED)
    s.multiline(620, 84, ["ROB", "重排序缓冲", "200+ 项"], 12)

    # Scheduler
    s.rect(700, 60, 120, 90, fill=LGREEN, stroke=GREEN)
    s.multiline(760, 84, ["调度器 RS", "按操作数就绪", "乱序挑选发射"], 11)

    # Ports
    ports = [("P0", "ALU/FMA"), ("P1", "ALU/FMA"), ("P5", "ALU/洗牌"), ("P2/P3", "Load"), ("P4/P7", "Store"), ("P6", "分支")]
    for i, (p, d) in enumerate(ports):
        x = 830
        y = 55 + i * 40
        s.rect(x, y, 130, 32, fill="#f6f9fd", stroke=BLUE, sw=1.2, rx=6)
        s.text(x + 65, y + 21, f"{p}  {d}", 11)
    s.arrow(820, 105, 828, 105, stroke=GRAY)

    # arrows main flow
    s.arrow(170, 105, 188, 105, sw=2)
    s.arrow(380, 105, 398, 105, sw=2)
    s.arrow(540, 105, 558, 105, sw=2)
    s.arrow(680, 105, 698, 105, sw=2)

    # Retire
    s.rect(560, 190, 120, 60, fill=LGREEN, stroke=GREEN)
    s.multiline(620, 212, ["退役 Retire", "按程序序提交"], 11)
    s.arrow(620, 150, 620, 188, stroke=GREEN, dash="4 3")
    s.text(700, 225, "只有退役的指令才算数（Retiring）", 11, anchor="start", fill=GRAY)

    # Memory subsystem
    s.text(250, 300, "内存子系统（Load/Store 的数据来源）", 13, bold=True, anchor="middle")
    mem = [("L1D", "32-48KB", "~4 周期", LGREEN, GREEN),
           ("L2", "1-2MB", "~14 周期", LBLUE, BLUE),
           ("LLC", "30MB+ 共享", "~40-70 周期", LORANGE, ORANGE),
           ("DRAM", "GB 级", "~200-300 周期", LRED, RED)]
    for i, (n, c, l, f, st) in enumerate(mem):
        x = 40 + i * 200
        s.rect(x, 320, 170, 70, fill=f, stroke=st)
        s.multiline(x + 85, 342, [n, c, l], 12)
        if i < 3:
            s.arrow(x + 170, 355, x + 198, 355, stroke=GRAY)
    s.arrow(90, 320, 90, 155, stroke=GREEN, dash="5 4")
    s.text(100, 240, "Load 数据上行", 11, anchor="start", fill=GRAY, rotate=-90)

    s.text(490, 430, "前端受限 = 译码/取指供不上 μop；后端受限 = 有 μop 但缺数据或执行资源", 12, fill=GRAY)
    s.save(os.path.join(OUT, "cpu-pipeline.svg"))


# ---------------------------------------------------------------- 3. memory hierarchy
def memory_hierarchy():
    s = SVG(760, 540)
    s.text(380, 30, "存储器层次结构金字塔：越往上越快越小越贵", 18, bold=True)
    levels = [
        ("寄存器", "约 1KB", "< 1 周期", GREEN,  140),
        ("L1 Cache", "32-64KB", "约 4 周期 (~1ns)", "#2ecc71", 220),
        ("L2 Cache", "1-2MB", "约 14 周期 (~4ns)", BLUE, 300),
        ("LLC (L3)", "30MB+ 共享", "约 40-70 周期", ORANGE, 380),
        ("DRAM 内存", "数十 GB", "约 200-300 周期 (~80ns)", RED, 460),
    ]
    y = 60
    for i, (n, cap, lat, color, w) in enumerate(levels):
        x = 260 - w / 2
        s.rect(x, y, w, 62, fill="#ffffff", stroke=color, sw=2.5, rx=6)
        s.text(x + w / 2, y + 26, n, 14, bold=True, fill=color)
        s.text(x + w / 2, y + 48, f"容量 {cap}   延迟 {lat}", 11, fill=GRAY)
        y += 76
    # 数量级标注
    s.rect(520, 130, 220, 240, fill="#fbfcfe", stroke=BLUE, sw=1.2)
    s.multiline(540, 155, [
        "关键直觉：",
        "L1 命中 ~ 1ns",
        "DRAM ~ 100ns",
        "相差约 100 倍！",
        "",
        "一次 LLC miss 的代价",
        "≈ 执行 200-300 条指令",
        "的时间被浪费",
    ], 12, anchor="start")
    s.text(380, 520, "优化的本质：让数据尽量停留在金字塔上层（缓存友好数据结构 / 分块 / 预取）", 12, fill=GRAY)
    s.save(os.path.join(OUT, "memory-hierarchy.svg"))


# ---------------------------------------------------------------- 4. TAGE
def tage():
    s = SVG(940, 500)
    s.text(470, 30, "TAGE 分支预测器：用几何级数递增的历史长度匹配分支规律", 18, bold=True)

    # global history
    s.rect(40, 60, 860, 46, fill=LGRAY, stroke=GRAY)
    s.text(470, 88, "全局分支历史寄存器（最近几十~几百个分支的跳转方向）", 13)

    # base predictor
    s.rect(40, 180, 180, 90, fill=LBLUE, stroke=BLUE)
    s.multiline(130, 205, ["基础预测器", "bimodal", "短历史"], 12)
    s.arrow(130, 106, 130, 178, stroke=BLUE)

    # tagged components
    comps = [("T1", "历史 4", GREEN), ("T2", "历史 8", ORANGE), ("T3", "历史 16", RED), ("Tn", "历史 100+", PURPLE)]
    for i, (n, h, c) in enumerate(comps):
        x = 260 + i * 170
        s.rect(x, 180, 150, 90, fill="#ffffff", stroke=c, sw=2)
        s.multiline(x + 75, 205, [f"{n} 带标签分量", h, "索引+tag 匹配"], 12)
        s.arrow(x + 75, 106, x + 75, 178, stroke=c, dash="4 3")

    # selector
    s.rect(300, 330, 340, 70, fill=LGREEN, stroke=GREEN, sw=2)
    s.multiline(470, 353, ["选择规则：命中分量中", "历史最长者的预测为准"], 13)
    for i in range(4):
        x = 335 + i * 170
        s.arrow(x, 270, 440 + (i - 1.5) * 30, 328, stroke=GRAY)
    s.arrow(130, 270, 330, 328, stroke=GRAY)

    # usefulness note
    s.rect(680, 330, 230, 110, fill="#fdf9f0", stroke=ORANGE, sw=1.2)
    s.multiline(700, 355, [
        "usefulness 计数器：",
        "· 新表项只在“长历史”",
        "  “更有用”时才分配",
        "· 防止表被无效项污染",
    ], 12, anchor="start")
    s.text(470, 475, "启示：数据值随机相关的分支不属于任何历史模式 → 只能靠无分支改写（branchless）", 12, fill=GRAY)
    s.save(os.path.join(OUT, "tage.svg"))


# ---------------------------------------------------------------- 5. measurement noise
def measurement_noise():
    s = SVG(840, 420)
    s.text(420, 30, "噪声环境下的测量分布：为什么最小值比均值更接近真实", 18, bold=True)

    # axes
    ox, oy = 100, 350
    s.arrow(ox, oy, 780, oy, stroke="#333")
    s.arrow(ox, oy, ox, 70, stroke="#333")
    s.text(430, 390, "测得的执行时间 →", 13)
    s.text(80, 200, "频次", 13, rotate=-90)

    import random
    random.seed(42)
    # distribution: main hump + right tail + small second hump
    def f(x):
        import math
        g1 = math.exp(-((x - 260) / 55) ** 2) * 230
        g2 = math.exp(-((x - 520) / 90) ** 2) * 70
        return g1 + g2 + 6
    pts = [(ox + x, oy - f(x)) for x in range(0, 660, 6)]
    s.polyline(pts, stroke=BLUE, sw=2.2)

    # true time
    tx = ox + 120
    s.line(tx, oy, tx, 110, stroke=GREEN, sw=2, dash="6 4")
    s.text(tx, 100, "真实时间（无干扰）", 12, fill=GREEN, bold=True)
    # minimum
    mx = ox + 168
    s.line(mx, oy, mx, 320, stroke=RED, sw=2)
    s.text(mx, 335, "最小值", 12, fill=RED, bold=True)
    s.text(mx, 305, "最接近真实！", 11, fill=RED)
    # mean
    ax_ = ox + 330
    s.line(ax_, oy, ax_, 240, stroke=ORANGE, sw=2, dash="3 3")
    s.text(ax_, 230, "均值（被噪声拖尾拉高）", 12, fill=ORANGE)

    # second hump annotation
    s.text(ox + 520, oy - 95, "第二峰：cache 未命中", 11, fill=PURPLE)
    s.text(ox + 520, oy - 80, "/ 系统中断干扰", 11, fill=PURPLE)

    s.text(420, 415, "噪声只会增加耗时（非负干扰）→ 多次测量取最小值 + IQR 评估噪声水平", 12, fill=GRAY)
    s.save(os.path.join(OUT, "measurement-noise.svg"))


# ---------------------------------------------------------------- 6. change point
def changepoint():
    s = SVG(880, 400)
    s.text(440, 30, "变点检测：从噪声时间序列中自动定位“性能回归是从哪个 commit 开始的”", 18, bold=True)

    ox, oy = 80, 330
    s.arrow(ox, oy, 830, oy, stroke="#333")
    s.arrow(ox, oy, ox, 60, stroke="#333")
    s.text(450, 375, "commit 时间线 →", 13)
    s.text(60, 200, "耗时", 13, rotate=-90)

    import random
    random.seed(7)
    pts = []
    for i in range(60):
        x = ox + 12 + i * 12
        base = 130 if i < 33 else 220
        y = oy - base + random.uniform(-18, 18)
        pts.append((x, y))
    s.polyline(pts, stroke=BLUE, sw=1.6)
    for x, y in pts:
        s.circle(x, y, 2.5, fill=BLUE)

    # means
    s.line(ox + 12, oy - 130, ox + 12 + 32 * 12, oy - 130, stroke=GREEN, sw=2.5)
    s.line(ox + 12 + 33 * 12, oy - 220, ox + 12 + 59 * 12, oy - 220, stroke=RED, sw=2.5)

    # change point
    cx = ox + 12 + 33 * 12
    s.line(cx, oy, cx, 70, stroke=RED, sw=2, dash="6 4")
    s.text(cx, 60, "变点：commit a1b2c3", 12, fill=RED, bold=True)
    s.text(cx + 130, 130, "回归！均值抬高", 12, fill=RED, anchor="start")

    s.text(200, 100, "正常波动（噪声）", 12, fill=GREEN)
    s.text(440, 395, "E-Divisive：用能量距离找“分布变化最大”的切分点，置换检验给 p 值 → 显著才报警", 11, fill=GRAY)
    s.save(os.path.join(OUT, "changepoint.svg"))


# ---------------------------------------------------------------- 7. eytzinger
def eytzinger():
    s = SVG(940, 480)
    s.text(470, 30, "二分查找的数据布局：同样的算法，不同的缓存命运", 18, bold=True)

    # left: sorted
    s.text(220, 65, "① 普通 sorted 数组 + 二分", 14, bold=True)
    vals = list(range(1, 16))
    for i, v in enumerate(vals):
        x = 60 + i * 22
        hot = v in (8, 4, 12)
        s.rect(x, 85, 22, 26, fill=(LORANGE if hot else "#f7f9fb"), stroke=(ORANGE if hot else GRAY), sw=1, rx=3)
        s.text(x + 11, 103, str(v), 10)
    s.multiline(220, 140, ["查找路径在数组中来回跳跃", "每一跳都是一次潜在 cache miss", "且下一步地址依赖比较结果 → 无法预取"], 11, fill=RED)

    # middle tree
    s.text(640, 65, "② 同一棵搜索树（逻辑结构）", 14, bold=True)
    tree = {0: (640, 95), 1: (570, 140), 2: (710, 140), 3: (535, 185), 4: (605, 185), 5: (675, 185), 6: (745, 185)}
    edges = [(0, 1), (0, 2), (1, 3), (1, 4), (2, 5), (2, 6)]
    for a, b in edges:
        s.line(*tree[a], *tree[b], stroke=GRAY, sw=1)
    labels = ["8", "4", "12", "2", "6", "10", "14"]
    for i, (x, y) in tree.items():
        s.circle(x, y, 14, fill=LBLUE, stroke=BLUE, sw=1.5)
        s.text(x, y + 4, labels[i], 11)

    # bottom: eytzinger array
    s.text(470, 260, "③ Eytzinger（BFS 层序）布局：节点 i 的孩子在 2i+1 / 2i+2，上层节点在内存中连续", 14, bold=True)
    arr = ["8", "4", "12", "2", "6", "10", "14", "1", "3", "5", "7", "9", "11", "13", "15"]
    for i, v in enumerate(arr):
        x = 110 + i * 48
        hot = i < 3
        s.rect(x, 285, 44, 34, fill=(LGREEN if hot else "#f7f9fb"), stroke=(GREEN if hot else GRAY), sw=(2 if hot else 1), rx=4)
        s.text(x + 22, 306, v, 12)
        s.text(x + 22, 336, f"[{i}]", 9, fill=GRAY)
    s.rect(110, 283, 136, 40, fill="none", stroke=GREEN, sw=2.5, rx=6, dash="5 3")
    s.text(178, 355, "前 3 层在同 1-2 个缓存行内", 11, fill=GREEN)

    s.multiline(470, 395, [
        "无分支更新：i = 2i + 1 + (a[i] < x)  →  搜索路径在出发前就完全确定",
        "于是可以提前 3-4 层发出软件预取：串行 cache miss → 并行 miss，大数组上快 2-3 倍",
    ], 12, fill="#333")
    s.save(os.path.join(OUT, "eytzinger.svg"))


# ---------------------------------------------------------------- 8. coz
def coz():
    s = SVG(820, 440)
    s.text(410, 30, "Coz 因果剖析：不问“时间花在哪”，问“加速这行能带来多少收益”", 18, bold=True)

    ox, oy = 90, 360
    s.arrow(ox, oy, 760, oy, stroke="#333")
    s.arrow(ox, oy, ox, 70, stroke="#333")
    s.text(430, 400, "对某行代码施加的“虚拟加速”幅度 →", 13)
    s.text(70, 210, "程序吞吐", 13, rotate=-90)

    # critical line
    pts = [(ox + x, oy - (30 + 1.9 * x)) for x in range(0, 620, 10)]
    s.polyline(pts, stroke=RED, sw=2.5)
    s.text(560, 150, "关键路径上的行", 13, fill=RED, bold=True)
    s.text(560, 170, "优化它有真实收益", 12, fill=RED)

    # non-critical
    s.line(ox + 10, oy - 60, ox + 630, oy - 60, stroke=GRAY, sw=2.5)
    s.text(480, 275, "非关键路径的行（可能很热但在空转等待）", 12, fill=GRAY)
    s.text(480, 293, "优化它收益为零", 12, fill=GRAY)

    # experiment points
    for x in (150, 300, 450, 600):
        s.circle(ox + x, oy - (30 + 1.9 * x), 4.5, fill=RED)

    s.multiline(410, 55, ["原理：要“虚拟加速”A 行，就在执行 A 行时令其他线程短暂停顿，再测吞吐变化",
                          "对每行 × 多个加速幅度做实验，拟合出“加速潜力曲线”"], 11, fill=BLUE)
    s.save(os.path.join(OUT, "coz.svg"))


# ---------------------------------------------------------------- 9. BOLT pipeline
def bolt():
    s = SVG(980, 300)
    s.text(490, 28, "BOLT 二进制布局优化流水线（Facebook/Meta）", 18, bold=True)
    steps = [
        ("生产环境采样", "perf record -b", "LBR 记录分支轨迹", LBLUE, BLUE),
        ("生成 profile", "perf2bolt", "采样→控制流计数", LGRAY, GRAY),
        ("BOLT 优化", "llvm-bolt", "CFG 重建/函数重排\n基本块重排/冷热拆分", LGREEN, GREEN),
        ("优化后二进制", "+大页(.text)", "I-Cache/ITLB miss 大降", LORANGE, ORANGE),
    ]
    x = 30
    for name, cmd, desc, f, st in steps:
        s.rect(x, 70, 200, 120, fill=f, stroke=st, sw=2)
        s.text(x + 100, 100, name, 14, bold=True, fill=st)
        s.text(x + 100, 124, cmd, 12)
        for j, ln in enumerate(desc.split("\n")):
            s.text(x + 100, 148 + j * 18, ln, 11, fill=GRAY)
        if x < 700:
            s.arrow(x + 200, 130, x + 238, 130, sw=2)
        x += 240
    s.multiline(490, 240, [
        "典型收益：HHVM、Clang 等大型应用提速 2-15%（已开 FDO+LTO 的 Clang 上仍有 2-6%）",
        "思想：编译期 profile 与最终二进制布局脱节 → 布局优化的“最后一公里”放在二进制层",
    ], 12, fill="#333")
    s.save(os.path.join(OUT, "bolt-pipeline.svg"))


# ---------------------------------------------------------------- 10. roofline
def roofline():
    s = SVG(760, 480)
    s.text(380, 30, "Roofline 模型：一张图分清“内存瓶颈”还是“计算瓶颈”", 18, bold=True)

    ox, oy = 100, 400
    s.arrow(ox, oy, 700, oy, stroke="#333")
    s.arrow(ox, oy, ox, 60, stroke="#333")
    s.text(400, 440, "算术强度（FLOP/Byte，对数轴）→", 13)
    s.text(75, 230, "性能 (GFLOPS)", 13, rotate=-90)

    # roofline
    peak_y = 110
    knee_x = 380
    s.line(ox + 30, oy - 30, knee_x, peak_y, stroke=BLUE, sw=3)
    s.line(knee_x, peak_y, 690, peak_y, stroke=RED, sw=3)
    s.text(220, 230, "内存带宽屋顶（斜率=带宽）", 12, fill=BLUE)
    s.text(530, 95, "算力屋顶（峰值 GFLOPS）", 12, fill=RED)

    # points
    s.circle(250, 300, 7, fill=ORANGE)
    s.text(250, 285, "应用 A", 12, bold=True)
    s.text(250, 330, "撞到带宽屋顶", 11, fill=BLUE)
    s.text(250, 346, "→ 优化内存访问（第 8 章）", 11, fill=GRAY)

    s.circle(560, 150, 7, fill=GREEN)
    s.text(560, 135, "应用 B", 12, bold=True)
    s.text(560, 175, "撞到算力屋顶", 11, fill=RED)
    s.text(560, 191, "→ 优化计算/向量化（第 9 章）", 11, fill=GRAY)
    s.save(os.path.join(OUT, "roofline.svg"))


# ---------------------------------------------------------------- 11. moore
def moore():
    s = SVG(860, 440)
    s.text(430, 30, "单线程性能增长的时代变迁（Hennessy 图灵奖讲座数据）", 18, bold=True)

    ox, oy = 90, 370
    s.arrow(ox, oy, 810, oy, stroke="#333")
    s.arrow(ox, oy, ox, 60, stroke="#333")
    s.text(450, 410, "年份 →", 13)
    s.text(65, 215, "年增长率 %", 13, rotate=-90)

    eras = [
        (1986, 2003, 52, "RISC/超标量黄金时代", GREEN),
        (2003, 2011, 23, "Dennard 缩放终结 → 转向多核", ORANGE),
        (2011, 2018, 12, "多核红利衰减", RED),
        (2018, 2023, 3, "性能翻倍需 20 年", PURPLE),
    ]
    x0, x1 = 1986, 2023
    W = 700

    def X(year):
        return ox + (year - x0) / (x1 - x0) * W

    for y0, y1v, g, label, c in eras:
        bx, ex = X(y0), X(y1v)
        by = oy - g * 5
        s.rect(bx, by, ex - bx, oy - by, fill="#ffffff", stroke=c, sw=2, rx=4, opacity=0.9)
        s.text((bx + ex) / 2, by + 22, f"每年 +{g}%", 14, bold=True, fill=c)
        s.text((bx + ex) / 2, by + 42, label, 10, fill=GRAY)
        s.text(bx, oy + 18, str(y0), 10, fill=GRAY)
    s.text(X(2023), oy + 18, "2023", 10, fill=GRAY)

    s.multiline(430, 75, ["通用核“免费提速”时代结束 → 性能红利转向软件性能工程（本书主题）与领域专用架构 DSA",
                          "Leiserson 矩阵乘案例：同一硬件上纯软件优化可获得 6 万倍加速"], 12, fill=BLUE)
    s.save(os.path.join(OUT, "moore.svg"))


# ---------------------------------------------------------------- 12. Cache CAT
def cache_cat():
    s = SVG(820, 400)
    s.text(410, 30, "Intel CAT 缓存分配技术：把 LLC 按 way 划分给不同业务", 18, bold=True)

    # LLC ways
    s.text(300, 75, "共享 LLC（如 20 way 组相联）", 13, bold=True)
    for i in range(20):
        x = 80 + i * 27
        if i < 12:
            f, st = LGREEN, GREEN
        else:
            f, st = LRED, RED
        s.rect(x, 90, 25, 60, fill=f, stroke=st, sw=1.2, rx=3)
        s.text(x + 12, 125, f"w{i}", 9)
    s.rect(80, 88, 12 * 27 - 2, 64, fill="none", stroke=GREEN, sw=2.5, rx=5)
    s.rect(80 + 12 * 27, 88, 8 * 27 - 2, 64, fill="none", stroke=RED, sw=2.5, rx=5)

    # COS
    s.rect(80, 190, 300, 80, fill=LGREEN, stroke=GREEN)
    s.multiline(230, 212, ["COS0：延迟敏感业务（在线服务）", "CBM 掩码 = 1111_1111_1100...", "独占 12 个 way → 尾延迟有保障"], 12)
    s.rect(440, 190, 300, 80, fill=LRED, stroke=RED)
    s.multiline(590, 212, ["COS1：批处理/后台任务", "CBM 掩码 = 0000_0000_0011...", "只用 8 个 way → 不污染邻居"], 12)

    s.rect(80, 300, 660, 70, fill="#fbfcfe", stroke=BLUE, sw=1.2)
    s.multiline(100, 322, [
        "实战（Linux resctrl）：mount -t resctrl resctrl /sys/fs/resctrl",
        "echo 'L3:0=fff;1=ff' > /sys/fs/resctrl/COS0/schemata   # 先监控（CMT/MBM）再分区",
    ], 11, anchor="start")
    s.save(os.path.join(OUT, "cache-cat.svg"))


if __name__ == "__main__":
    tma_tree(); print("tma-tree ok")
    cpu_pipeline(); print("cpu-pipeline ok")
    memory_hierarchy(); print("memory-hierarchy ok")
    tage(); print("tage ok")
    measurement_noise(); print("measurement-noise ok")
    changepoint(); print("changepoint ok")
    eytzinger(); print("eytzinger ok")
    coz(); print("coz ok")
    bolt(); print("bolt-pipeline ok")
    roofline(); print("roofline ok")
    moore(); print("moore ok")
    cache_cat(); print("cache-cat ok")
    print("all done ->", os.path.abspath(OUT))
