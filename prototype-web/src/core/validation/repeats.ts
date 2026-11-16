import type MusicData from '@/core/types/MusicData'
import type { MeasureInfo } from './time'
import { measureTimeline } from './time'

export interface PlayedMeasure {
  info: MeasureInfo
  /** How many times this measure has been played so far, starting at 1. */
  pass: number
  /** Played-order tick of the measure start. */
  startTick: number
}

interface VoltaRange { start: number, end: number, passes: number[] }

/**
 * Unrolls repeats, voltas and jumps into the order the measures are played.
 * Assumed here, because the data does not say: "to-coda" and "fine" act at the end of
 * their measure; after a jump with `playRepeats: false` only the last volta is played.
 */
export const expandRepeats = (
  music: MusicData,
  timeline: MeasureInfo[] = measureTimeline(music),
): PlayedMeasure[] => {
  const measures = music.measures
  const indexOf = new Map(measures.map((measure, index) => [measure.id, index]))
  const markerMeasure = (id: string) => measures.findIndex(m => m.markers.some(k => k.id === id))

  const voltas: VoltaRange[] = music.globalSpanners
    .filter(spanner => spanner.kind === 'volta')
    .map(volta => ({
      start: indexOf.get(volta.startMeasureId) ?? -1,
      end: indexOf.get(volta.endMeasureId) ?? -1,
      passes: volta.passes,
    }))
    .filter(volta => volta.start >= 0 && volta.end >= volta.start)
    .sort((a, b) => a.start - b.start)

  // The last volta of each run of adjacent voltas.
  const finalVoltas = new Set<VoltaRange>()
  voltas.forEach((volta, i) => {
    const next = voltas[i + 1]
    if (!next || next.start !== volta.end + 1) finalVoltas.add(volta)
  })

  const played: PlayedMeasure[] = []
  const counts = measures.map(() => 0)
  const takenJumps = new Set<string>()
  const finishedRepeats = new Set<number>()
  let jump: { until?: string, then?: string, playRepeats: boolean } | undefined
  let sectionStart = 0
  let iteration = 1
  let tick = 0
  let i = 0

  for (let guard = 0; i >= 0 && i < measures.length && guard < 10000; guard += 1) {
    const measure = measures[i]!
    const info = timeline[i]!
    if (measure.repeatStart && sectionStart !== i) {
      sectionStart = i
      iteration = 1
    }

    const skipRepeats = jump !== undefined && !jump.playRepeats
    const volta = voltas.find(v => v.start <= i && i <= v.end)
    const voltaPlays = !volta
      || (skipRepeats ? finalVoltas.has(volta) : volta.passes.includes(iteration))
    if (!voltaPlays) {
      i += 1
      continue
    }

    counts[i]! += 1
    played.push({ info, pass: counts[i]!, startTick: tick })
    tick += info.lengthTicks

    if (jump?.until !== undefined && measure.markers.some(m => m.id === jump!.until)) {
      const then = jump.then
      jump = undefined
      if (then === undefined) break
      i = markerMeasure(then)
      continue
    }

    if (measure.repeatEnd && !skipRepeats && !finishedRepeats.has(i)) {
      if (iteration < measure.repeatEnd.times) {
        iteration += 1
        i = sectionStart
        continue
      }
      finishedRepeats.add(i)
    }

    const next = measure.jumps.find(j => !takenJumps.has(j.id))
    if (next) {
      takenJumps.add(next.id)
      jump = {
        playRepeats: next.playRepeats,
        ...(next.until !== undefined ? { until: next.until } : {}),
        ...(next.then !== undefined ? { then: next.then } : {}),
      }
      if (next.playRepeats) {
        finishedRepeats.clear()
        iteration = 1
      }
      i = next.to.type === 'start' ? 0 : markerMeasure(next.to.id)
      continue
    }

    i += 1
  }
  return played
}
