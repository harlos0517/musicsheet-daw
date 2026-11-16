import type { CurveEventOverride } from '@/core/types/EventOverride'
import type { CurveRule, DynamicMapping } from '@/core/types/InterpretationProfile'
import type { ElementAnchor, TickAnchor } from '@/core/types/MusicData'
import type { CurveEvent, CurveParameter } from '@/core/types/MusicEvent'
import type { TrackContext } from './buildEvents'
import { clamp01, curveKey, evaluateCurve, segmentValue } from './curves'
import { applyMode, DEFAULT_INITIAL_DYNAMICS } from './interpretation'
import { absTick, resolveAnchor } from './scoreIndex'

type Applies = NonNullable<CurveEvent['appliesTo']>

type Segment =
  & Pick<CurveEvent, 'parameter' | 'startTick' | 'durationTicks' | 'fromValue' | 'toValue'>
  & Partial<Pick<CurveEvent, 'appliesTo' | 'combine' | 'curveType' | 'steepness'>>

/** A segment in written time (no repeats), before it is copied into played time. */
interface Written {
  event: CurveEvent
  /** Passes its source is limited to. */
  passes: number[] | undefined
}

interface Item { tick: number, order: number, run: () => void }

const applyOverride = (event: CurveEvent, override: CurveEventOverride) => {
  if (override.parameter !== undefined) event.parameter = override.parameter
  if (override.appliesTo !== undefined) event.appliesTo = override.appliesTo
  if (override.startTickOffset !== undefined) {
    event.startTick += override.startTickOffset
    event.durationTicks = Math.max(0, event.durationTicks - override.startTickOffset)
  }
  if (override.durationTicks) {
    const { mode, value } = override.durationTicks
    event.durationTicks = Math.max(0, applyMode(mode, event.durationTicks, value))
  }
  if (override.fromValue)
    event.fromValue = applyMode(override.fromValue.mode, event.fromValue, override.fromValue.value)

  if (override.toValue)
    event.toValue = applyMode(override.toValue.mode, event.toValue, override.toValue.value)

  if (override.curveType !== undefined) event.curveType = override.curveType
  if (override.steepness !== undefined) event.steepness = override.steepness
}

const overridesOf = (ctx: TrackContext) => ctx.profiles.overrides.filter(
  (o): o is CurveEventOverride => o.kind === 'curve-event-override',
)

/** Value a curve has where nothing has been written yet. */
const initialValue = (ctx: TrackContext, parameter: CurveParameter): number =>
  parameter === 'dynamics' ? ctx.profile.initialDynamics ?? DEFAULT_INITIAL_DYNAMICS : 0

/**
 * Step 1: every curve segment of the track in written order, as the page reads from left
 * to right. Each segment starts from what is written before it, never from what happened
 * to be played before it. Overrides without `passes` are applied here, so the segments
 * after an overridden one start from the overridden value.
 */
const writeCurves = (ctx: TrackContext): Written[] => {
  const { track, profile, index, timeline, music, warnings } = ctx
  const written: Written[] = []
  const events: CurveEvent[] = []
  const items: Item[] = []
  const overrides = overridesOf(ctx).filter(override => !override.passes)

  const scopeOf = (staffIds?: string[], voiceIndexes?: number[]): Applies | undefined => {
    if (!staffIds && !voiceIndexes) return undefined
    const staves = staffIds ?? track.staves.map(staff => staff.id)
    if (!voiceIndexes) return staves.map(staffId => ({ staffId }))
    return staves.flatMap(staffId => voiceIndexes.map(voiceIndex => ({ staffId, voiceIndex })))
  }

  /** Base level of a curve, without accents added on top. */
  const current = (parameter: CurveParameter, tick: number, applies: Applies | undefined) => {
    const first = applies?.[0]
    const scope = first ? { staffId: first.staffId, voiceIndex: first.voiceIndex ?? 0 } : undefined
    return evaluateCurve(events, parameter, tick, initialValue(ctx, parameter), scope, 'base')
  }

  const emit = (
    sourceId: string, segment: number, fields: Segment, passes?: number[],
  ) => {
    const event: CurveEvent = {
      kind: 'curve-event',
      id: '',
      sourceIds: [sourceId],
      trackId: track.id,
      pass: 0,
      segmentIndex: segment,
      ...fields,
    }
    for (const override of overrides) {
      if (override.sourceId === sourceId && override.segmentIndex === segment)
        applyOverride(event, override)

    }
    events.push(event)
    written.push({ event, passes })
  }

  const tickOf = (anchor: ElementAnchor | TickAnchor, atEnd = false) => {
    const position = resolveAnchor(index, anchor, atEnd)
    return position && absTick(timeline, position)
  }

  // Dynamics
  const levels: { tick: number, mapping: DynamicMapping }[] = []
  for (const attachment of track.attachments) {
    if (attachment.kind !== 'dynamic') continue
    const tick = tickOf(attachment.at)
    const mapping = profile.dynamics?.find(d => d.type === attachment.type)
    if (tick === undefined || !mapping) {
      warnings.push(`dynamic ${attachment.id}: no mapping for "${attachment.type}"`)
      continue
    }
    const appliesTo = scopeOf(attachment.staffIds, attachment.voiceIndexes)
    const then = mapping.then
    const thenTicks = (then?.afterQn ?? 0) * music.tpq
    levels.push({ tick, mapping })
    items.push({
      tick,
      order: attachment.beforeTick ? 0 : 1,
      run: () => {
        const before = current('dynamics', tick, appliesTo)
        const peak = clamp01(applyMode(mapping.mode, before, mapping.value))
        const after = then ? clamp01(applyMode(then.mode, before, then.value)) : peak
        // `combine: 'add'` puts the dynamic on top of the level as an offset, so a
        // hairpin running underneath is not interrupted (an accent such as sfz).
        const added = mapping.combine === 'add'
        const base = added ? before : 0
        const segment = {
          parameter: 'dynamics' as const,
          startTick: tick,
          ...(appliesTo ? { appliesTo } : {}),
          ...(added ? { combine: 'add' as const } : {}),
        }
        emit(attachment.id, 0, {
          ...segment, durationTicks: 0, fromValue: before - base, toValue: peak - base,
        })
        if (!then || thenTicks <= 0) return
        emit(attachment.id, 1, {
          ...segment,
          durationTicks: thenTicks,
          fromValue: peak - base,
          toValue: after - base,
          curveType: then.curveType,
        })
      },
    })
  }

  // Hairpins
  const hairpinSetup = profile.hairpin ?? { curveType: 'linear' as const, defaultChange: 0.2 }
  for (const spanner of track.trackSpanners) {
    if (spanner.kind !== 'hairpin') continue
    const start = tickOf(spanner.start)
    const end = tickOf(spanner.end, spanner.includeEnd === true)
    if (start === undefined || end === undefined) continue
    const applies = scopeOf(spanner.staffIds)
    items.push({
      tick: start,
      order: 2,
      run: () => {
        const rising = spanner.type === 'cresc'
        const from = spanner.niente && rising ? 0 : current('dynamics', start, applies)
        // The target is a fixed-level dynamic sitting exactly where the hairpin ends.
        const target = levels.find(d => d.tick === end && d.mapping.mode === 'set')
        let to = target?.mapping.value ?? from + (rising ? 1 : -1) * hairpinSetup.defaultChange
        if (spanner.niente && !rising) to = 0
        emit(spanner.id, 0, {
          parameter: 'dynamics',
          startTick: start,
          durationTicks: end - start,
          fromValue: from,
          toValue: clamp01(to),
          curveType: hairpinSetup.curveType,
          ...(hairpinSetup.steepness !== undefined ? { steepness: hairpinSetup.steepness } : {}),
          ...(applies ? { appliesTo: applies } : {}),
        })
      },
    })
  }

  // Curve rules: pedals, custom spanners and technique markings
  interface Source {
    id: string
    rule: CurveRule
    start: number
    end: number
    applies: Applies | undefined
    passes: number[] | undefined
  }
  const sources: Source[] = []
  for (const rule of profile.curveRules) {
    for (const spanner of track.trackSpanners) {
      const isPedal = rule.match.kind === 'pedal' && spanner.kind === 'pedal'
        && spanner.type === rule.match.type
      const isCustom = rule.match.kind === 'technique' && spanner.kind === 'custom-spanner'
        && spanner.techniqueId === rule.match.techniqueId
      if (!isPedal && !isCustom) continue
      if (spanner.kind !== 'pedal' && spanner.kind !== 'custom-spanner') continue
      const start = tickOf(spanner.start)
      const end = tickOf(spanner.end, spanner.includeEnd === true)
      if (start === undefined || end === undefined) continue
      const custom = spanner.kind === 'custom-spanner' ? spanner : undefined
      sources.push({
        id: spanner.id,
        rule,
        start,
        end,
        applies: scopeOf(custom?.staffIds, custom?.voiceIndexes),
        passes: custom?.passes,
      })
    }
    for (const attachment of track.attachments) {
      if (rule.match.kind !== 'technique' || attachment.kind !== 'technique') continue
      if (attachment.techniqueId !== rule.match.techniqueId) continue
      const tick = tickOf(attachment.at)
      if (tick === undefined) continue
      sources.push({
        id: attachment.id,
        rule,
        // A marking has no length: only its time-0 point produces a segment.
        start: tick,
        end: tick,
        applies: scopeOf(attachment.staffIds, attachment.voiceIndexes),
        passes: attachment.passes,
      })
    }
  }
  sources.sort((a, b) => a.start - b.start)

  for (const source of sources) {
    const { rule, start, end, applies } = source
    items.push({
      tick: start,
      order: 2,
      run: () => {
        const before = current(rule.parameter, start, applies)
        const low = rule.startValue ?? before
        // "Next value": the start value the next span on the same curve gives explicitly.
        const next = sources.find(other => other !== source && other.start >= end
          && curveKey(other.rule.parameter) === curveKey(rule.parameter))
        const high = rule.endValue ?? next?.rule.startValue ?? 1
        const length = end - start
        let previousTime = 0
        let previousValue = before
        rule.points.forEach((point, segment) => {
          if (length === 0 && point.time > 0) return
          const time = Math.min(1, Math.max(previousTime, point.time))
          const durationTicks = Math.round(length * (time - previousTime))
          const value = clamp01(low + (high - low) * point.value)
          emit(source.id, segment, {
            parameter: rule.parameter,
            startTick: start + Math.round(length * previousTime),
            durationTicks,
            fromValue: previousValue,
            toValue: value,
            ...(durationTicks > 0 && point.curveType ? { curveType: point.curveType } : {}),
            ...(durationTicks > 0 && point.steepness !== undefined
              ? { steepness: point.steepness }
              : {}),
            ...(applies ? { appliesTo: applies } : {}),
          }, source.passes)
          previousTime = time
          previousValue = value
        })
      },
    })
  }

  items.sort((a, b) => a.tick - b.tick || a.order - b.order)
  for (const item of items) item.run()
  return written
}

/**
 * Step 2: copies the written segments into played time. The played measures are cut into
 * runs that follow the page without a jump. Each run first restores the state the page
 * has at its first barline (so a repeat or D.S. lands in the right dynamic, pedal, …,
 * even in the middle of a hairpin), then plays the segments written inside it.
 */
const playCurves = (ctx: TrackContext, written: Written[]): CurveEvent[] => {
  const { played, timeline, track } = ctx
  const overrides = overridesOf(ctx).filter(override => override.passes)
  const result: CurveEvent[] = []
  const usedIds = new Map<string, number>()

  const push = (event: CurveEvent) => {
    const source = event.sourceIds[0] ?? `${track.id}:${curveKey(event.parameter)}`
    const id = `${source}@${event.pass}#${event.segmentIndex}`
    const count = (usedIds.get(id) ?? 0) + 1
    usedIds.set(id, count)
    // The same segment can be entered twice in one pass of different measures.
    result.push({ ...event, id: count === 1 ? id : `${id}~${count}` })
  }

  // Segments that can replace each other: same curve, same layer, same voices.
  const groups = new Map<string, Written[]>()
  for (const item of written) {
    const { parameter, combine, appliesTo: applies } = item.event
    const key = `${curveKey(parameter)}|${combine ?? ''}|${JSON.stringify(applies ?? null)}`
    groups.set(key, [...(groups.get(key) ?? []), item])
  }

  const scoreEnd = timeline.at(-1)!.startTick + timeline.at(-1)!.lengthTicks
  let from = 0
  while (from < played.length) {
    let to = from
    while (played[to + 1]?.info.index === played[to]!.info.index + 1) to += 1
    const first = played[from]!
    const last = played[to]!
    const runStart = first.info.startTick
    const runEnd = last.info.startTick + last.info.lengthTicks
    const shift = first.startTick - runStart
    const passAt = (tick: number) => played.slice(from, to + 1)
      .findLast(measure => measure.info.startTick <= tick)?.pass ?? first.pass

    /** Copy of a written segment from `start`, cut at the end of the run. */
    const copy = (event: CurveEvent, start: number, pass: number): CurveEvent => {
      const eventEnd = event.startTick + event.durationTicks
      const end = Math.min(eventEnd, runEnd)
      return {
        ...event,
        pass,
        startTick: start + shift,
        durationTicks: Math.max(0, end - start),
        fromValue: start > event.startTick ? segmentValue(event, start) : event.fromValue,
        toValue: end < eventEnd ? segmentValue(event, end) : event.toValue,
      }
    }

    // State at the first barline of the run. Not needed at the very beginning.
    if (from > 0 || runStart > 0) {
      const entries: { order: number, event: CurveEvent }[] = []
      for (const group of groups.values()) {
        const inForce = group.findLast(item => item.event.startTick < runStart)?.event
        const sample = group[0]!.event
        if (!inForce) {
          const value = sample.combine === 'add' ? 0 : initialValue(ctx, sample.parameter)
          entries.push({
            order: -1,
            event: {
              ...sample,
              sourceIds: [],
              segmentIndex: 0,
              pass: first.pass,
              startTick: first.startTick,
              durationTicks: 0, fromValue: value, toValue: value,
            },
          })
        } else if (inForce.startTick + inForce.durationTicks > runStart)
          entries.push({ order: inForce.startTick, event: copy(inForce, runStart, first.pass) })

        else {
          entries.push({
            order: inForce.startTick,
            event: {
              ...inForce, pass: first.pass, startTick: first.startTick, durationTicks: 0,
              fromValue: inForce.toValue,
            },
          })
        }
      }
      entries.sort((a, b) => a.order - b.order).forEach(entry => push(entry.event))
    }

    for (const { event, passes } of written) {
      const atScoreEnd = runEnd === scoreEnd && event.startTick === runEnd
      if (event.startTick < runStart || (event.startTick >= runEnd && !atScoreEnd)) continue
      const pass = passAt(event.startTick)
      if (passes && !passes.includes(pass)) continue
      const copied = copy(event, event.startTick, pass)
      for (const override of overrides) {
        if (override.sourceId !== event.sourceIds[0]) continue
        if (override.segmentIndex !== event.segmentIndex) continue
        if (override.passes!.includes(pass)) applyOverride(copied, override)
      }
      push(copied)
    }
    from = to + 1
  }

  return result.sort((a, b) => a.startTick - b.startTick)
}

/** Every curve segment of one track, in played order. */
export const buildCurves = (ctx: TrackContext): CurveEvent[] =>
  playCurves(ctx, writeCurves(ctx))
