// API 客户端：与 benchd REST API 对应的类型与请求函数。

export interface Run {
  id: number
  project_id: number
  commit_sha: string
  ref: string
  label: string
  ci_provider: string
  ci_run_url: string
  started_at: string
  finished_at: string
}

export interface CompareResult {
  benchmark: string
  kind: string
  unit: string
  n_before: number
  n_after: number
  median_before: number
  median_after: number
  speedup: number
  delta_pct: number
  ci_low: number
  ci_high: number
  p_value: number
  significant: boolean
  note: string
}

export interface CompareResponse {
  before_run: number
  after_run: number
  env_warning: string
  results: CompareResult[]
  counters: Record<string, { before: Record<string, number>; after: Record<string, number> }>
}

export interface BenchSamples {
  benchmark: string
  kind: string
  unit: string
  values: number[]
}

async function getJSON<T>(path: string): Promise<T> {
  const resp = await fetch(path)
  if (!resp.ok) throw new Error(`${path}: HTTP ${resp.status}`)
  return resp.json()
}

export const fetchRuns = (project = '') =>
  getJSON<{ runs: Run[] | null }>(`/api/v1/runs?project=${encodeURIComponent(project)}&limit=200`)
    .then((r) => r.runs ?? [])

export const fetchCompare = (before: number, after: number) =>
  getJSON<CompareResponse>(`/api/v1/compare?before_run=${before}&after_run=${after}`)

export const fetchSamples = (runId: number) =>
  getJSON<{ samples: BenchSamples[] | null }>(`/api/v1/runs/${runId}/samples`)
    .then((r) => r.samples ?? [])

// 差分火焰图节点（与 internal/flame.Node 的 JSON 对应）
export interface FlameNode {
  name: string
  before: number
  after: number
  children: FlameNode[] | null
}

export const fetchFlameDiff = (before: number, after: number, benchmark: string) =>
  getJSON<FlameNode>(
    `/api/v1/flamegraph/diff?before_run=${before}&after_run=${after}&benchmark=${encodeURIComponent(benchmark)}`,
  )

// 时间格式化：ns → 人类可读
export function formatNs(ns: number): string {
  if (ns >= 1e9) return (ns / 1e9).toFixed(3) + ' s'
  if (ns >= 1e6) return (ns / 1e6).toFixed(3) + ' ms'
  if (ns >= 1e3) return (ns / 1e3).toFixed(3) + ' µs'
  return ns.toFixed(1) + ' ns'
}

// 四分位数（ECharts boxplot 需要 [min, Q1, median, Q3, max]）
export function boxData(values: number[]): number[] {
  const s = [...values].sort((a, b) => a - b)
  const q = (p: number) => {
    const idx = (s.length - 1) * p
    const lo = Math.floor(idx)
    const hi = Math.ceil(idx)
    return s[lo] + (s[hi] - s[lo]) * (idx - lo)
  }
  return [s[0], q(0.25), q(0.5), q(0.75), s[s.length - 1]]
}
