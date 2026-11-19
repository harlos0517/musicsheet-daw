import type MusicData from '@/core/types/MusicData'
import type { PlayedMeasure } from '@/core/validation/repeats'
import type { MeasureInfo } from '@/core/validation/time'
import { durationTicks } from '@/core/validation/time'
import type { ScoreIndex } from './scoreIndex'
import { absTick, resolveAnchor } from './scoreIndex'

export interface TempoPoint { tick: number, qpm: number }

export interface TempoMap {
  /** Tempo changes in played order, in quarter notes per minute. */
  points: TempoPoint[]
  tickToMs: (tick: number) => number
  msToTick: (ms: number) => number
}

const DEFAULT_QPM = 120
/** A rit. / accel. becomes one tempo step per this fraction of a quarter note. */
const RAMP_STEPS_PER_QUARTER = 4

/**
 * The tempo is resolved in written order first: every measure has the tempo the page
 * shows there, also when it is reached by a repeat or a jump. Those written tempo points
 * are then copied into played time, run by run. A gradual change without `targetTempo`
 * ends at 75 % (rit.) or 125 % (accel.) of the tempo it starts from.
 */
export const buildTempoMap = (
  music: MusicData,
  played: PlayedMeasure[],
  index: ScoreIndex,
  timeline: MeasureInfo[],
): TempoMap => {
  const tpq = music.tpq

  // Written tempo points.
  const written: TempoPoint[] = []
  for (const info of timeline) {
    for (const tempo of info.measure.tempos) {
      written.push({
        tick: info.startTick + tempo.tick,
        qpm: tempo.bpm * durationTicks(tempo.unit, tpq) / tpq,
      })
    }
  }
  written.sort((a, b) => a.tick - b.tick)
  const marked = [...written]
  const markedAt = (tick: number) =>
    marked.findLast(point => point.tick <= tick)?.qpm ?? marked[0]?.qpm ?? DEFAULT_QPM

  // Ramp points go in front of the marks, so a tempo mark written where a ramp ends
  // (an "a tempo" after a rit.) is what counts from there on.
  const ramps: TempoPoint[] = []
  for (const spanner of music.globalSpanners) {
    if (spanner.kind !== 'tempo-change') continue
    const start = resolveAnchor(index, spanner.start)
    const end = resolveAnchor(index, spanner.end)
    if (!start || !end) continue
    const startTick = absTick(timeline, start)
    const endTick = absTick(timeline, end)
    const from = markedAt(startTick)
    const to = spanner.targetTempo ?? from * (spanner.type === 'rit' ? 0.75 : 1.25)
    const step = tpq / RAMP_STEPS_PER_QUARTER
    for (let tick = startTick; tick < endTick; tick += step) {
      const middle = Math.min(endTick, tick + step / 2)
      const progress = (middle - startTick) / (endTick - startTick)
      ramps.push({ tick, qpm: from + (to - from) * progress })
    }
    ramps.push({ tick: endTick, qpm: to })
  }
  written.unshift(...ramps)
  written.sort((a, b) => a.tick - b.tick)
  const writtenAt = (tick: number) =>
    written.findLast(point => point.tick <= tick)?.qpm ?? written[0]?.qpm ?? DEFAULT_QPM

  // Played tempo points: each run of measures that follows the page starts with the
  // tempo written at its first barline, then takes the points written inside it.
  const points: TempoPoint[] = []
  const scoreEnd = timeline.at(-1)!.startTick + timeline.at(-1)!.lengthTicks
  let first = 0
  while (first < played.length) {
    let last = first
    while (played[last + 1]?.info.index === played[last]!.info.index + 1) last += 1
    const runStart = played[first]!.info.startTick
    const runEnd = played[last]!.info.startTick + played[last]!.info.lengthTicks
    const shift = played[first]!.startTick - runStart
    points.push({ tick: runStart + shift, qpm: writtenAt(runStart) })
    for (const point of written) {
      const inRun = point.tick > runStart && point.tick < runEnd
      if (inRun || (point.tick === runEnd && runEnd === scoreEnd))
        points.push({ tick: point.tick + shift, qpm: point.qpm })

    }
    first = last + 1
  }
  if (points.length === 0) points.push({ tick: 0, qpm: DEFAULT_QPM })

  const msPerTick = (qpm: number) => 60000 / (qpm * tpq)
  const starts: number[] = []
  points.forEach((point, i) => {
    const previous = points[i - 1]
    starts.push(previous
      ? starts[i - 1]! + (point.tick - previous.tick) * msPerTick(previous.qpm)
      : 0)
  })

  return {
    points,
    tickToMs: tick => {
      const i = Math.max(0, points.findLastIndex(point => point.tick <= tick))
      return starts[i]! + (tick - points[i]!.tick) * msPerTick(points[i]!.qpm)
    },
    msToTick: ms => {
      const i = Math.max(0, starts.findLastIndex(start => start <= ms))
      return points[i]!.tick + (ms - starts[i]!) / msPerTick(points[i]!.qpm)
    },
  }
}
