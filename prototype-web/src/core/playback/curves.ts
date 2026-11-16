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
 * Value of a curve at a tick. A curve has two layers: the base, where the segment that
 * started last decides and its end value holds, and the `combine: 'add'` segments, whose
 * value is added on top the same way. `layer: 'base'` leaves the added layer out.
 * Without `scope`, segments limited to some voices count too.
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
  let base: CurveEvent | undefined
  let added: CurveEvent | undefined
  for (const event of events) {
    if (curveKey(event.parameter) !== key || event.startTick > tick) continue
    if (!covers(event, scope)) continue
    if (event.combine === 'add') {
      if (!added || event.startTick >= added.startTick) added = event
    } else if (!base || event.startTick >= base.startTick)
      base = event

  }
  const value = base ? segmentValue(base, tick) : fallback
  return layer === 'total' && added ? value + segmentValue(added, tick) : value
}
