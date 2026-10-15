import { useEffect, useRef } from 'react'
import * as echarts from 'echarts'

// EChart：ECharts 的轻量 React 封装（option 变化即重绘，窗口缩放自适应）。
export default function EChart({ option, height = 320 }: { option: echarts.EChartsOption; height?: number }) {
  const ref = useRef<HTMLDivElement>(null)
  const chartRef = useRef<echarts.ECharts>()

  useEffect(() => {
    if (!ref.current) return
    chartRef.current = echarts.init(ref.current)
    const onResize = () => chartRef.current?.resize()
    window.addEventListener('resize', onResize)
    return () => {
      window.removeEventListener('resize', onResize)
      chartRef.current?.dispose()
    }
  }, [])

  useEffect(() => {
    chartRef.current?.setOption(option, true)
  }, [option])

  return <div ref={ref} style={{ width: '100%', height }} />
}
