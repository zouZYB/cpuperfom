import { useEffect, useState } from 'react'
import {
  BenchSamples, CompareResponse, Run,
  fetchCompare, fetchRuns, fetchSamples,
} from './api'
import CompareView from './components/CompareView'

// App：运行列表（选 before/after）+ 对比视图。
export default function App() {
  const [runs, setRuns] = useState<Run[]>([])
  const [beforeId, setBeforeId] = useState<number>()
  const [afterId, setAfterId] = useState<number>()
  const [compare, setCompare] = useState<CompareResponse>()
  const [beforeSamples, setBeforeSamples] = useState<BenchSamples[]>([])
  const [afterSamples, setAfterSamples] = useState<BenchSamples[]>([])
  const [error, setError] = useState('')
  const [loading, setLoading] = useState(false)

  useEffect(() => {
    fetchRuns().then(setRuns).catch((e) => setError(String(e)))
  }, [])

  const doCompare = async () => {
    if (!beforeId || !afterId) return
    setLoading(true)
    setError('')
    try {
      const [c, bs, as] = await Promise.all([
        fetchCompare(beforeId, afterId),
        fetchSamples(beforeId),
        fetchSamples(afterId),
      ])
      setCompare(c)
      setBeforeSamples(bs)
      setAfterSamples(as)
    } catch (e) {
      setError(String(e))
    } finally {
      setLoading(false)
    }
  }

  return (
    <div className="container">
      <h1>cpuperfom 性能基准对比</h1>
      {error && <div className="warning">⚠ {error}</div>}

      <h2>运行记录</h2>
      <table className="data-table">
        <thead>
          <tr>
            <th>优化前</th><th>优化后</th><th>ID</th><th>标签</th>
            <th>Commit</th><th>分支</th><th>开始时间</th>
          </tr>
        </thead>
        <tbody>
          {runs.map((r) => (
            <tr key={r.id}>
              <td><input type="radio" name="before" checked={beforeId === r.id}
                onChange={() => setBeforeId(r.id)} /></td>
              <td><input type="radio" name="after" checked={afterId === r.id}
                onChange={() => setAfterId(r.id)} /></td>
              <td>{r.id}</td>
              <td><span className={`label label-${r.label}`}>{r.label}</span></td>
              <td>{r.commit_sha.slice(0, 12)}</td>
              <td>{r.ref}</td>
              <td>{r.started_at.replace('T', ' ').slice(0, 19)}</td>
            </tr>
          ))}
        </tbody>
      </table>
      {runs.length === 0 && <p className="muted">暂无运行记录，先用 bench-runner run 上报数据</p>}

      <button className="primary" disabled={!beforeId || !afterId || loading} onClick={doCompare}>
        {loading ? '对比中…' : '开始对比'}
      </button>

      {compare && (
        <>
          <h2>对比结果（run {compare.before_run} → run {compare.after_run}）</h2>
          <CompareView data={compare} beforeSamples={beforeSamples} afterSamples={afterSamples} />
        </>
      )}
    </div>
  )
}
