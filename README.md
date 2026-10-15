# cpuperfom — C/C++ 性能基准 CI 平台

直观展示**优化前后**性能差异的基准平台：函数级（Google Benchmark）+ 程序级（整命令计时）双粒度，
带统计显著性检验（中位数 / bootstrap 置信区间 / Mann-Whitney U），支持 Linux（perf 硬件计数器）、
Windows 原生、WSL。

## 架构

```
┌──────────────┐   REST    ┌──────────────┐
│ bench-runner │ ────────► │    benchd    │  SQLite（WAL）
│ （跨平台执行） │           │ （API 服务器） │
└──────────────┘           └──────────────┘
```

- `cmd/benchd` — 平台服务器：结果存储、对比 API（M2 起含 Web 看板）
- `cmd/bench-runner` — 基准执行器：跑 bench.yaml、环境快照、计数器采集、上报/本地对比
- `internal/compare` — 统计引擎：中位数、加速比、bootstrap CI、Mann-Whitney U 检验
- `examples/matmul-demo` — 演示：naive（缓存不友好 i-j-k）vs tiled（分块）矩阵乘法

## 快速开始

```bash
# 1. 构建
go build -o bin/benchd ./cmd/benchd
go build -o bin/bench-runner ./cmd/bench-runner

# 2. 启动服务器（可选 BENCHD_TOKEN 环境变量启用写接口认证）
./bin/benchd -addr :8080 -db cpuperfom.db
# Web 看板: http://localhost:8080/

# 3. 构建演示项目
cd examples/matmul-demo
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j

# 4. 跑优化前/后基准并上报
bench-runner run -c bench-before.yaml --server http://localhost:8080 --label before -o before.json
bench-runner run -c bench-after.yaml  --server http://localhost:8080 --label after  -o after.json

# 5. 对比（在线或本地）
bench-runner compare --server http://localhost:8080 --before 1 --after 2
bench-runner compare --before-file before.json --after-file after.json
```

## 查看监控数据

```bash
# 历史运行列表
bench-runner runs --server http://localhost:8080 [--project matmul-demo] [--limit 50]

# 对比视图（计时 + 硬件计数器对比表）
bench-runner compare --server http://localhost:8080 --before 1 --after 2

# REST API
curl "http://localhost:8080/api/v1/runs?project=matmul-demo"   # 运行列表
curl http://localhost:8080/api/v1/runs/1                        # 单次运行详情
curl http://localhost:8080/api/v1/runs/1/counters               # 该次运行的计数器
curl "http://localhost:8080/api/v1/compare?before_run=1&after_run=2"  # 对比 JSON

# 直接查 SQLite（高级）
sqlite3 cpuperfom.db "SELECT * FROM runs ORDER BY id DESC LIMIT 5;"
```

示例输出（matmul-demo，naive → tiled）：

```
BENCHMARK  BEFORE     AFTER     Δ%       SPEEDUP  95% CI            p       结论
app        562.939ms  52.887ms  -90.61%  10.644x  [10.337, 11.023]  <0.001  ✓ 变快 90.6%
matmul     532.542ms  28.133ms  -94.72%  18.929x  [17.841, 19.970]  <0.001  ✓ 变快 94.7%
```

## bench.yaml

```yaml
project: my-project
benchmarks:
  - name: matmul          # 函数级：命令需输出 Google Benchmark JSON
    kind: func            #   runner 自动补 --benchmark_format=json，
    command: build/bench --benchmark_filter=BM_X --benchmark_repetitions=10
  - name: app             # 程序级：整命令墙钟计时，重复 repeat 次
    kind: prog
    command: build/app --input data/x
    repeat: 20
counters: [cycles, instructions, cache-misses, branch-misses]  # Linux + perf 时生效
```

## 统计口径

- 每侧样本 ≥10 才出显著性结论，否则提示"样本不足"
- 显著 = p < 0.05（Mann-Whitney U）且 |Δ| ≥ 5%
- 加速比 95% CI 由 bootstrap（2000 次重采样）给出
- 两次运行环境（CPU/governor/编译器）不一致时输出警告

## 硬件计数器后端

runner 自动探测可用后端（prog 级基准自动附带采集）：

| 平台 | 后端 | 指标 |
|------|------|------|
| Linux | `perf stat` | cycles / instructions / cache-misses / branch-misses + 派生 IPC |
| Windows/Linux (AMD) | `AMDuProfPcm`（uProf 5.x） | cycles / instructions / **IPC** / branch_miss_pti / l2_miss_pti / **L3 Miss%** / **L3 Miss 延迟** / Eff Freq |
| 其他 | 无 | 仅计时数据，不阻断运行 |

uProf 后端说明：
- 查找顺序：环境变量 `CPUPERFOM_UPROF_PCM` → PATH → `C:\Program Files\AMD\AMDuProf\bin\AMDuProfPcm.exe`
- 已自动处理的坑：目标程序转绝对路径、PMC MSR 占用自动 `-r` 重置重试、输出重定向到文件避免挂起、`-a` 全核采集
- 采集窗口与被测程序生命周期绑定（程序退出即停止），指标为该窗口系统级聚合值
- 也可用 `AMDuProfPcm compare` 对两次采集生成 HTML 对比报告

## 平台注意事项

- **Windows**：计数器走 AMD uProf（Intel CPU 上 uProf PMU 功能受限，退化为纯计时）；MinGW 构建建议 `-static` 链接避免 DLL ABI 冲突
- **Linux**：计数器采集需要 `perf`；建议 `cpupower frequency-set -g performance` 锁频
- 时间戳统一 RFC3339 文本存储（SQLite 无原生时间类型）

## Web 看板

前端（React + ECharts）嵌入 benchd 单二进制，访问 `http://localhost:8080/`：
- 运行记录列表：单选 before/after → 一键对比
- 对比表格：Δ%、加速比、95% CI、p 值、显著性结论
- 样本箱线图：优化前后分布对比
- 计数器变化图：各指标 Δ% 横向条形图

```bash
# 修改前端后重新构建（产物嵌入 Go 二进制，需重编 benchd）
cd web && npm install && npm run build && cd ..
go build -o bin/benchd ./cmd/benchd

# 前端开发模式（API 自动代理到 :8080）
cd web && npm run dev
```

## 路线图

- [x] M1 核心闭环：runner → API → 存储 → CLI 对比表
- [x] M2a Web 看板：运行列表、对比表格、箱线图、计数器面板
- [ ] M2b ABAB 交替、git worktree 双版本、差分火焰图
- [ ] M3 趋势与回归：baseline、阈值告警
- [ ] M4 权限与多项目
- [ ] M5 CI 集成（GitHub Actions / GitLab CI / Jenkins）与通知
