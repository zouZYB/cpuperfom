import { useEffect, useMemo, useState } from 'react'
import { FlameNode, fetchFlameDiff } from '../api'

// DiffFlameGraph：自绘 SVG 差分火焰图。
// 宽度 = max(before, after) 样本占比；颜色 = 差异方向（红=变慢，蓝=变快，灰=不变）。
const FRAME_H = 18
const PAD_TOP = 8

export default function DiffFlameGraph({
  beforeRun,
  afterRun,
  benchmark,
}: {
  beforeRun: number
  afterRun: number
  benchmark: string
}) {
  const [tree, setTree] = useState<FlameNode | null>(null)
  const [error, setError] = useState('')

  useEffect(() => {
    setError('')
    setTree(null)
    fetchFlameDiff(beforeRun, afterRun, benchmark)
      .then(setTree)
      .catch(() => setError('该基准无火焰图数据'))
  }, [beforeRun, afterRun, benchmark])

  const frames = useMemo(() => (tree ? layout(tree) : []), [tree])
  if (error) return <p className="muted">{error}</p>
  if (!tree) return <p className="muted">加载中…</p>

  const depth = frames.reduce((d, f) => Math.max(d, f.depth), 0)
  const height = (depth + 1) * FRAME_H + PAD_TOP

  return (
    <div className="flame-wrap">
      <svg viewBox={`0 0 1000 ${height}`} width="100%" height={height} role="img">
        {frames.map((f, i) => (
          <g key={i}>
            <rect x={f.x} y={f.y} width={Math.max(f.w - 0.3, 0)} height={FRAME_H - 1} fill={f.color}>
              <title>{f.tooltip}</title>
            </rect>
            {f.w > 50 && (
              <text x={f.x + 3} y={f.y + 13} fontSize={11} fill="#222" pointerEvents="none">
                {truncate(f.node.name, f.w / 7)}
              </text>
            )}
          </g>
        ))}
      </svg>
      <div className="flame-legend">
        <span><i style={{ background: diffColor(0.5) }} /> 变慢</span>
        <span><i style={{ background: diffColor(0) }} /> 基本不变</span>
        <span><i style={{ background: diffColor(-0.5) }} /> 变快</span>
        <span className="muted">（悬停查看 before/after 样本数与 Δ%）</span>
      </div>
    </div>
  )
}

interface Frame {
  node: FlameNode
  x: number
  y: number
  w: number
  depth: number
  color: string
  tooltip: string
}

// 布局：深度优先展开，x 按子节点占比累加，根在底部。
function layout(root: FlameNode): Frame[] {
  const frames: Frame[] = []
  const total = Math.max(root.before, root.after, 1)

  const walk = (node: FlameNode, x: number, depth: number) => {
    const value = Math.max(node.before, node.after)
    const w = (value / total) * 1000
    if (w < 0.5) return // 过窄不渲染（后端已 Prune，双保险）
    const ratio = diffRatio(node)
    frames.push({
      node,
      x,
      y: PAD_TOP + 0, // 占位，深度确定后统一翻转
      w,
      depth,
      color: diffColor(ratio),
      tooltip: `${node.name}\n优化前 ${node.before} 样本 / 优化后 ${node.after} 样本\nΔ ${(ratio * 100).toFixed(1)}%`,
    })
    if (!node.children) return
    let cx = x
    for (const c of node.children) {
      const cw = (Math.max(c.before, c.after) / total) * 1000
      walk(c, cx, depth + 1)
      cx += cw
    }
  }
  walk(root, 0, 0)

  const maxDepth = frames.reduce((d, f) => Math.max(d, f.depth), 0)
  for (const f of frames) {
    f.y = PAD_TOP + (maxDepth - f.depth) * FRAME_H // 翻转：根在底部
  }
  return frames
}

function diffRatio(n: FlameNode): number {
  if (n.before === 0) return n.after > 0 ? 1 : 0 // 新增代码视为变慢
  return (n.after - n.before) / n.before
}

// 红=变慢，蓝=变快，强度随 |Δ| 饱和到 ±50%
function diffColor(ratio: number): string {
  const t = Math.max(-1, Math.min(1, ratio / 0.5))
  if (Math.abs(t) < 0.1) return '#c9c9c9'
  if (t > 0) {
    const r = Math.round(220 + 35 * t)
    const gb = Math.round(190 - 120 * t)
    return `rgb(${Math.min(r, 255)},${gb},${gb})`
  }
  const b = Math.round(220 + 35 * -t)
  const rg = Math.round(190 - 120 * -t)
  return `rgb(${rg},${rg},${Math.min(b, 255)})`
}

function truncate(s: string, max: number): string {
  return s.length > max ? s.slice(0, Math.max(max - 1, 1)) + '…' : s
}
