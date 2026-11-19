import type { CurveEvent, CurveParameter, CurveType } from '@/core/types/MusicEvent'

export interface CurveScope { staffId: string, voiceIndex: number }

export const curveKey = (parameter: CurveParameter): string =>
  typeof parameter === 'string' ? parameter : `custom:${parameter.name}`

export const clamp01 = (value: number): number => Math.min(1, Math.max(0, value))

/** Slow start: t^(1 + a). Used mirrored for a slow end. */
const easeIn = (t: number, a: number): number => t ** (1 + a)

/**
 * Progress 0–1 of a segment at time 0–1. `a` is the steepness: 0 is a straight line,
 * a positive value starts slowly, a negative one starts fast.
 *   exp:   (e^(a·t) − 1) / (e^a − 1)                              default a = 3
 *   power: t^(1 + a) for a > 0, 1 − (1 − t)^(1 − a) for a < 0      default a = 1
 *   ease:  the power curve on each half, mirrored at the middle   default a = 1
 *          (a > 0: slow – fast – slow, a < 0: fast – slow – fast)
 */
export const shape = (type: CurveType | undefined, a: number | undefined, t: number): number => {
  switch (type ?? 'linear') {
    case 'hold': return t >= 1 ? 1 : 0
    case 'exp': {
      const k = a ?? 3
      return k === 0 ? t : (Math.exp(k * t) - 1) / (Math.exp(k) - 1)
    }
    case 'power': {
      const k = a ?? 1
      return k >= 0 ? easeIn(t, k) : 1 - easeIn(1 - t, -k)
    }
    case 'ease': {
      const k = a ?? 1
      const half = (x: number) => k >= 0 ? easeIn(x, k) : 1 - easeIn(1 - x, -k)
      return t < 0.5 ? half(2 * t) / 2 : 1 - half(2 * (1 - t)) / 2
    }
    default: return t
  }
}

/** Value of one segment at a tick; its end value holds afterwards. */
export const segmentValue = (event: CurveEvent, tick: number): number => {
  const end = event.startTick + event.durationTicks
  if (event.durationTicks <= 0 || tick >= end) return event.toValue
  const t = (tick - event.startTick) / event.durationTicks
  const progress = shape(event.curveType, event.steepness, t)
  return event.fromValue + (event.toValue - event.fromValue) * progress
}

const covers = (event: CurveEvent, scope: CurveScope | undefined): boolean =>
  !scope || !event.appliesTo || event.appliesTo.some(a =>
    a.staffId === scope.staffId
    && (a.voiceIndex === undefined || a.voiceIndex === scope.voiceIndex))

/**
 * Value of a curve at a tick. A curve has three layers, each decided by the segment that
 * started last, whose end value then holds:
 *   base:  dynamics, hairpins and other written levels
 *   scale: `combine: 'scale'` segments, which multiply the base (swells)
 *   add:   `combine: 'add'` segments, added on top (accents)
 * `layer: 'base'` gives the base alone. Without `scope`, segments limited to some
 * voices count too.
 */
export const evaluateCurve = (
  events: CurveEvent[],
  parameter: CurveParameter,
  tick: number,
  fallback: number,
  scope?: CurveScope,
  layer: 'total' | 'base' = 'total',
): number => {
  const key = curveKey(parameter)
  const latest: Partial<Record<'base' | 'scale' | 'add', CurveEvent>> = {}
  for (const event of events) {
    if (curveKey(event.parameter) !== key || event.startTick > tick) continue
    if (!covers(event, scope)) continue
    const name = layerOf(event)
    const found = latest[name]
    if (!found || event.startTick >= found.startTick) latest[name] = event
  }
  return combine(latest, tick, fallback, layer)
}

const layerOf = (event: CurveEvent) =>
  event.combine === 'add' ? 'add' : event.combine === 'scale' ? 'scale' : 'base'

const combine = (
  latest: Partial<Record<'base' | 'scale' | 'add', CurveEvent>>,
  tick: number,
  fallback: number,
  layer: 'total' | 'base',
): number => {
  const base = latest.base ? segmentValue(latest.base, tick) : fallback
  if (layer === 'base') return base
  const scale = latest.scale ? segmentValue(latest.scale, tick) : 1
  const add = latest.add ? segmentValue(latest.add, tick) : 0
  return base * scale + add
}

/**
 * The same as `evaluateCurve` for every voice at once, for ticks asked in increasing
 * order. It walks the segments once instead of scanning them for every tick, which
 * matters for curves with thousands of segments.
 */
export const curveSampler = (
  events: CurveEvent[],
  parameter: CurveParameter,
  fallback: number,
) => {
  const key = curveKey(parameter)
  const sorted = events.filter(event => curveKey(event.parameter) === key)
    .sort((a, b) => a.startTick - b.startTick)
  const latest: Partial<Record<'base' | 'scale' | 'add', CurveEvent>> = {}
  let next = 0
  return (tick: number): number => {
    while (next < sorted.length && sorted[next]!.startTick <= tick) {
      const event = sorted[next]!
      latest[layerOf(event)] = event
      next += 1
    }
    return combine(latest, tick, fallback, 'total')
  }
}
