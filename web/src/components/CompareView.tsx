import { useState } from 'react'
import { BenchSamples, CompareResponse, boxData, formatNs } from '../api'
import EChart from './EChart'
import DiffFlameGraph from './DiffFlameGraph'

// CompareView：优化前后对比视图 = 对比表格 + 样本箱线图 + 计数器变化图 + 差分火焰图。
export default function CompareView({
  data,
  beforeSamples,
  afterSamples,
}: {
  data: CompareResponse
  beforeSamples: BenchSamples[]
  afterSamples: BenchSamples[]
}) {
  const [flameBench, setFlameBench] = useState('')
  const selected = flameBench || data.results[0]?.benchmark || ''

  return (
    <div>
      {data.env_warning && <div className="warning">⚠ {data.env_warning}</div>}
      <CompareTable data={data} />
      <h3>样本分布（箱线图）</h3>
      <BoxPlot data={data} beforeSamples={beforeSamples} afterSamples={afterSamples} />
      {Object.keys(data.counters ?? {}).length > 0 && (
        <>
          <h3>硬件计数器变化</h3>
          <CountersChart data={data} />
        </>
      )}
      <h3>差分火焰图（红=变慢 蓝=变快）</h3>
      <label>
        基准：
        <select value={selected} onChange={(e) => setFlameBench(e.target.value)}>
          {data.results.map((r) => (
            <option key={r.benchmark} value={r.benchmark}>{r.benchmark}</option>
          ))}
        </select>
      </label>
      {selected && (
        <DiffFlameGraph beforeRun={data.before_run} afterRun={data.after_run} benchmark={selected} />
      )}
    </div>
  )
}

function CompareTable({ data }: { data: CompareResponse }) {
  return (
    <table className="data-table">
      <thead>
        <tr>
          <th>基准</th><th>优化前</th><th>优化后</th><th>Δ%</th><th>加速比</th>
          <th>95% CI</th><th>p</th><th>结论</th>
        </tr>
      </thead>
      <tbody>
        {data.results.map((r) => (
          <tr key={r.benchmark}>
            <td>{r.benchmark} <span className="kind">{r.kind}</span></td>
            <td>{formatNs(r.median_before)}</td>
            <td>{formatNs(r.median_after)}</td>
            <td className={r.delta_pct < 0 ? 'good' : 'bad'}>{r.delta_pct.toFixed(2)}%</td>
            <td>{r.speedup.toFixed(3)}x</td>
            <td>[{r.ci_low.toFixed(3)}, {r.ci_high.toFixed(3)}]</td>
            <td>{r.p_value < 0.001 ? '<0.001' : r.p_value.toFixed(3)}</td>
            <td><Verdict r={r} /></td>
          </tr>
        ))}
      </tbody>
    </table>
  )
}

function Verdict({ r }: { r: CompareResponse['results'][0] }) {
  if (r.significant && r.delta_pct < 0) return <span className="good">✓ 变快 {(-r.delta_pct).toFixed(1)}%</span>
  if (r.significant && r.delta_pct > 0) return <span className="bad">✗ 变慢 {r.delta_pct.toFixed(1)}%</span>
  return <span className="muted">≈ {r.note || '无显著差异'}</span>
}

function BoxPlot({
  data, beforeSamples, afterSamples,
}: {
  data: CompareResponse
  beforeSamples: BenchSamples[]
  afterSamples: BenchSamples[]
}) {
  const categories: string[] = []
  const beforeBox: number[][] = []
  const afterBox: number[][] = []
  for (const r of data.results) {
    const b = beforeSamples.find((s) => s.benchmark === r.benchmark)
    const a = afterSamples.find((s) => s.benchmark === r.benchmark)
    if (!b || !a || b.values.length === 0 || a.values.length === 0) continue
    categories.push(r.benchmark)
    beforeBox.push(boxData(b.values))
    afterBox.push(boxData(a.values))
  }
  if (categories.length === 0) return <p className="muted">无样本数据</p>

  return (
    <EChart
      option={{
        tooltip: {
          trigger: 'item',
          formatter: (p: any) =>
            `${p.seriesName} ${p.name}<br/>max ${formatNs(p.data[5])}<br/>Q3 ${formatNs(p.data[4])}<br/>中位 ${formatNs(p.data[3])}<br/>Q1 ${formatNs(p.data[2])}<br/>min ${formatNs(p.data[1])}`,
        },
        legend: { data: ['优化前', '优化后'] },
        xAxis: { type: 'category', data: categories },
        yAxis: { type: 'value', axisLabel: { formatter: (v: number) => formatNs(v) } },
        series: [
          { name: '优化前', type: 'boxplot', data: beforeBox, itemStyle: { color: '#f4a261' } },
          { name: '优化后', type: 'boxplot', data: afterBox, itemStyle: { color: '#2a9d8f' } },
        ],
      }}
    />
  )
}

function CountersChart({ data }: { data: CompareResponse }) {
  // 汇总所有基准的指标 Δ%，横向条形图：负(绿)=下降，正(红)=上升
  const rows: { label: string; delta: number }[] = []
  for (const [bench, c] of Object.entries(data.counters)) {
    const metrics = new Set([...Object.keys(c.before), ...Object.keys(c.after)])
    for (const m of metrics) {
      const bv = c.before[m] ?? 0
      const av = c.after[m] ?? 0
      if (bv !== 0) rows.push({ label: `${bench} · ${m}`, delta: ((av - bv) / bv) * 100 })
    }
  }
  if (rows.length === 0) return null
  rows.sort((a, b) => a.delta - b.delta)

  return (
    <EChart
      height={Math.max(200, rows.length * 32)}
      option={{
        tooltip: { trigger: 'axis' },
        grid: { left: 220 },
        xAxis: { type: 'value', axisLabel: { formatter: '{value}%' } },
        yAxis: { type: 'category', data: rows.map((r) => r.label) },
        series: [{
          type: 'bar',
          data: rows.map((r) => ({
            value: +r.delta.toFixed(2),
            itemStyle: { color: r.delta < 0 ? '#2a9d8f' : '#e76f51' },
          })),
          label: { show: true, position: 'right', formatter: '{c}%' },
        }],
      }}
    />
  )
}
