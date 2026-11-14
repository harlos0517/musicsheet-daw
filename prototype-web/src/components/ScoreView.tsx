import { useEffect, useRef, useState } from 'react'

import type MusicData from '@/core/types/MusicData'
import { type RenderResult, loadScoreFonts, renderScore } from '@/render/renderScore'

interface ScoreViewProps {
  music: MusicData
  onRendered?: (result: RenderResult) => void
}

/** Renders a score with VexFlow, re-rendering when the data or the available width changes. */
export const ScoreView = ({ music, onRendered }: ScoreViewProps) => {
  const wrapper = useRef<HTMLDivElement>(null)
  const target = useRef<HTMLDivElement>(null)
  const [width, setWidth] = useState(0)
  const [fontsReady, setFontsReady] = useState(false)

  useEffect(() => {
    let cancelled = false
    void loadScoreFonts().then(() => !cancelled && setFontsReady(true))
    return () => {
      cancelled = true
    }
  }, [])

  useEffect(() => {
    const element = wrapper.current
    if (!element) return
    const observer = new ResizeObserver(([entry]) => {
      if (entry) setWidth(Math.floor(entry.contentRect.width))
    })
    observer.observe(element)
    return () => observer.disconnect()
  }, [])

  useEffect(() => {
    if (!fontsReady || width === 0 || !target.current) return
    const result = renderScore(target.current, music, { width })
    onRendered?.(result)
  }, [music, width, fontsReady, onRendered])

  return (
    <div ref={wrapper} className="score-view">
      <div ref={target} />
    </div>
  )
}

export default ScoreView
