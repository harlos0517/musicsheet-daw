import type { NoteEventOverride } from '@/core/types/EventOverride'
import type InterpretationProfile from '@/core/types/InterpretationProfile'
import type { NoteRule, NoteRuleAction } from '@/core/types/InterpretationProfile'
import type MusicData from '@/core/types/MusicData'
import type { Chord, DynamicType, Note, Track, UnpitchedNote } from '@/core/types/MusicData'
import { ArticulationType } from '@/core/types/MusicData'
import type {
  CurveEvent, MusicEvent, NoteEvent, SlurPosition,
} from '@/core/types/MusicEvent'
import { midiNumber } from '@/core/validation/pitch'
import type { PlayedMeasure } from '@/core/validation/repeats'
import { expandRepeats } from '@/core/validation/repeats'
import { tieTargets } from '@/core/validation/ties'
import type { MeasureInfo } from '@/core/validation/time'
import { durationTicks, measureTimeline } from '@/core/validation/time'
import type { TempoMap } from './tempo'
import { buildTempoMap } from './tempo'
import type { PlaybackSetup } from '../types/PlaybackSetup'
import { buildCurves } from './buildCurves'
import { buildSwells } from './swells'
import { clamp01, evaluateCurve } from './curves'
import {
  applyMode, DEFAULT_GRACE_NOTES, DEFAULT_INITIAL_DYNAMICS, resolveInterpretationProfile,
} from './interpretation'
import type { Placed, ScoreIndex } from './scoreIndex'
import { absTick, indexScore, resolveAnchor } from './scoreIndex'

export interface TrackContext {
  music: MusicData
  profiles: PlaybackSetup
  timeline: MeasureInfo[]
  played: PlayedMeasure[]
  index: ScoreIndex
  track: Track
  profile: InterpretationProfile
  /** The score's tempo, which is part of the music and so known to the rules. */
  tempo: TempoMap
  warnings: string[]
}

export interface EventResult {
  played: PlayedMeasure[]
  tempo: TempoMap
  events: MusicEvent[]
  warnings: string[]
}

/** A tremolo that stays one note: the patch plays it. */
const ROLL_TECHNIQUES = new Set(['core:tremolo', 'core:measured-tremolo'])

/** Techniques the engine turns into notes itself; instruments never see them. */
const GENERATED = new Set(['core:arpeggio', 'core:gliss'])

/** What a rule can match on besides the event itself. */
interface NoteContext {
  articulations: ArticulationType[]
  grace?: { slash: boolean }
  /** Notated length of the note right before this one in its voice, if they touch. */
  previousTicks?: number
  /** Played tick the note is written on, before any rule moves it. */
  beat: number
  /** Dynamics written on the note's chord. */
  dynamics?: DynamicType[]
  /** Hairpins that run over the note's written span. */
  hairpins?: ('cresc' | 'dim')[]
}

/**
 * Lengths in milliseconds come out of the tempo map with rounding errors (125.00000001):
 * a note exactly at a rule's limit must still count as inside it.
 */
const MS_TOLERANCE = 0.001

const matches = (
  rule: NoteRule, event: NoteEvent, context: NoteContext, tpq: number, tempo: TempoMap,
): boolean => {
  const { match, when } = rule
  const notatedQn = event.notatedTicks / tpq
  // Real length, for rules about what is fast or slow to play: a sixteenth at 60 and an
  // eighth at 120 are the same thing to a player.
  const at = context.beat
  if (when?.maxNotatedMs !== undefined) {
    const length = tempo.tickToMs(at + event.notatedTicks) - tempo.tickToMs(at)
    if (length > when.maxNotatedMs + MS_TOLERANCE) return false
  }
  if (when?.maxPreviousNotatedMs !== undefined) {
    const previous = context.previousTicks
    if (previous === undefined) return false
    const length = tempo.tickToMs(at) - tempo.tickToMs(at - previous)
    if (length > when.maxPreviousNotatedMs + MS_TOLERANCE) return false
  }
  if (when?.maxNotatedQn !== undefined && notatedQn > when.maxNotatedQn) return false
  if (when?.minNotatedQn !== undefined && notatedQn < when.minNotatedQn) return false
  if (when?.maxPreviousNotatedQn !== undefined) {
    const previous = context.previousTicks
    if (previous === undefined || previous / tpq > when.maxPreviousNotatedQn) return false
  }
  if (when?.slurPosition !== undefined && event.slur !== when.slurPosition) return false
  if (when?.hairpin !== undefined && !context.hairpins?.includes(when.hairpin)) return false
  if (when?.passes && !when.passes.includes(event.pass)) return false
  switch (match.kind) {
    case 'articulation': return context.articulations.includes(match.type)
    case 'technique': return event.techniques.includes(match.techniqueId)
    case 'slur':
      return event.slur !== undefined
        && (match.position === undefined || match.position === event.slur)
    case 'grace':
      return context.grace !== undefined
        && (match.slash === undefined || match.slash === context.grace.slash)
    case 'dynamic': return context.dynamics?.includes(match.type) === true
    default: return true
  }
}

/**
 * `fullTicks` is the length the note has before any rule: its notated length, less what
 * grace notes took. "Scale of notated" is measured against it. Lengths in a profile are
 * in quarter notes, so `tpq` turns them into ticks.
 */
const act = (event: NoteEvent, action: NoteRuleAction, tpq: number, fullTicks: number) => {
  switch (action.op) {
    case 'startOffset': {
      const offset = Math.min(action.qn * tpq, event.durationTicks - 1)
      event.startTick += offset
      event.durationTicks -= offset
      return
    }
    case 'addTechnique':
      if (!event.techniques.includes(action.techniqueId)) event.techniques.push(action.techniqueId)
      return
    case 'removeTechnique':
      event.techniques = event.techniques.filter(id => id !== action.techniqueId)
      return
    case 'rollAttack': return // adds a note: done by the caller
    default:
  }
  if (action.for === 'velocity') {
    event.velocity = clamp01(applyMode(action.op, event.velocity, action.value))
    return
  }
  let ticks = event.durationTicks
  if (action.op === 'setQn') ticks = action.value * tpq
  else if (action.op === 'setRatio') ticks = fullTicks * action.value
  else if (action.op === 'add') ticks += action.value * tpq
  else ticks *= action.value
  event.durationTicks = Math.max(1, ticks)
}

const override = (event: NoteEvent, overrides: NoteEventOverride[]) => {
  for (const o of overrides) {
    if (!event.sourceIds.includes(o.sourceId)) continue
    if (o.passes && !o.passes.includes(event.pass)) continue
    if (o.pitch !== undefined) event.pitch = o.pitch
    if (o.startTickOffset !== undefined) {
      event.startTick += o.startTickOffset
      event.durationTicks = Math.max(1, event.durationTicks - o.startTickOffset)
    }
    if (o.durationTicks) {
      const { mode, value } = o.durationTicks
      event.durationTicks = Math.max(1, applyMode(mode, event.durationTicks, value))
    }
    if (o.velocity)
      event.velocity = clamp01(applyMode(o.velocity.mode, event.velocity, o.velocity.value))

    if (o.techniques) {
      const removed = o.techniques.remove ?? []
      event.techniques = [
        ...new Set([...event.techniques.filter(id => !removed.includes(id)),
          ...(o.techniques.add ?? [])]),
      ]
    }
    if (o.startOffsetMs !== undefined) event.startOffsetMs = o.startOffsetMs
  }
}

/**
 * The notes of a track, and the phrase each note is in as written: its place in a slur
 * before a repeated pitch splits the slur for playing. Swells follow the written phrase.
 */
const buildNotes = (
  ctx: TrackContext, curves: CurveEvent[],
): { notes: NoteEvent[], phrases: Map<string, SlurPosition> } => {
  const { music, profiles, timeline, played, index, track, profile } = ctx
  const tpq = music.tpq
  const initialDynamics = profile.initialDynamics ?? DEFAULT_INITIAL_DYNAMICS
  const chords = index.chords.get(track.id) ?? []
  const overrides = profiles.overrides.filter(
    (o): o is NoteEventOverride => o.kind === 'note-event-override',
  )
  const groupOf = (id: string) => profiles.techniques.find(t => t.id === id)?.group ?? id

  // Notes that continue a tie, with the chord the tie comes from.
  const tieSources = new Map<string, Placed<Chord>>()
  for (const chord of chords) {
    for (const note of chord.event.notes)
      for (const target of tieTargets(note)) tieSources.set(target, chord)

  }

  // Technique markings and spanners, placed in written order.
  const inScope = (
    placed: Placed, staffIds: string[] | undefined, voiceIndexes: number[] | undefined,
  ) => (!staffIds || staffIds.includes(placed.staffId))
    && (!voiceIndexes || voiceIndexes.includes(placed.voiceIndex))

  const markings = track.attachments
    .flatMap(attachment => {
      if (attachment.kind !== 'technique') return []
      const position = resolveAnchor(index, attachment.at)
      return position ? [{ marking: attachment, tick: absTick(timeline, position) }] : []
    })
    .sort((a, b) => a.tick - b.tick)

  const customSpans = track.trackSpanners.flatMap(spanner => {
    if (spanner.kind !== 'custom-spanner') return []
    const start = resolveAnchor(index, spanner.start)
    const end = resolveAnchor(index, spanner.end, spanner.includeEnd === true)
    if (!start || !end) return []
    return [{ spanner, start: absTick(timeline, start), end: absTick(timeline, end) }]
  })

  // Dynamics by the tick they are written at, to tell a rule which note carries one.
  const dynamicMarks = track.attachments.flatMap(attachment => {
    if (attachment.kind !== 'dynamic' || attachment.beforeTick) return []
    const position = resolveAnchor(index, attachment.at)
    return position ? [{ attachment, tick: absTick(timeline, position) }] : []
  })
  const dynamicsFor = (placed: Placed<Chord>): DynamicType[] => dynamicMarks
    .filter(({ attachment, tick }) => tick === placed.absTick
      && inScope(placed, attachment.staffIds, attachment.voiceIndexes))
    .map(({ attachment }) => attachment.type)

  // Hairpins in written order, to tell a rule that a note sits under one.
  const hairpinSpans = track.trackSpanners.flatMap(spanner => {
    if (spanner.kind !== 'hairpin') return []
    const start = resolveAnchor(index, spanner.start)
    const end = resolveAnchor(index, spanner.end, spanner.includeEnd === true)
    if (!start || !end) return []
    return [{ spanner, start: absTick(timeline, start), end: absTick(timeline, end) }]
  })
  const hairpinsFor = (placed: Placed<Chord>) => hairpinSpans
    .filter(({ spanner, start, end }) => start < placed.absTick + placed.ticks
      && end > placed.absTick && inScope(placed, spanner.staffIds, undefined))
    .map(({ spanner }) => spanner.type)

  // Two-note tremolos: both chords sound together for the length of the two.
  const tremoloPairs = new Map<string, { start: Placed<Chord>, end: Placed<Chord> }>()
  for (const spanner of track.trackSpanners) {
    if (spanner.kind !== 'custom-spanner' || spanner.techniqueId !== 'core:tremolo-pair') continue
    if (spanner.start.type !== 'element' || spanner.end.type !== 'element') continue
    const start = index.elements.get(spanner.start.id)
    const end = index.elements.get(spanner.end.id)
    if (start?.event.kind !== 'chord' || end?.event.kind !== 'chord') continue
    const pair = { start: start as Placed<Chord>, end: end as Placed<Chord> }
    tremoloPairs.set(start.event.id, pair)
    tremoloPairs.set(end.event.id, pair)
  }

  /**
   * Techniques in force on a chord. Resolved in written order, so a jump back lands in
   * the state the page shows there, not the state the music was in before the jump.
   */
  const techniquesFor = (placed: Placed<Chord>, pass: number): string[] => {
    const lasting = new Map<string, string>()
    const result: string[] = []
    const ids = [placed.event.id, ...placed.event.notes.map(note => note.id)]
    for (const { marking, tick } of markings) {
      if (marking.passes && !marking.passes.includes(pass)) continue
      if (!inScope(placed, marking.staffIds, marking.voiceIndexes)) continue
      if (marking.scope === 'this') {
        if (ids.includes(marking.at.id)) result.push(marking.techniqueId)
      } else if (tick <= placed.absTick)
        lasting.set(groupOf(marking.techniqueId), marking.techniqueId)

    }
    for (const { spanner, start, end } of customSpans) {
      if (spanner.passes && !spanner.passes.includes(pass)) continue
      if (!inScope(placed, spanner.staffIds, spanner.voiceIndexes)) continue
      if (start <= placed.absTick && placed.absTick < end) result.push(spanner.techniqueId)
    }
    return [...new Set([...lasting.values(), ...result])]
  }

  // Slur membership: chords in the voice of either end, between the two ends.
  const slurPositions = new Map<string, SlurPosition>()
  for (const spanner of track.trackSpanners) {
    if (spanner.kind !== 'slur') continue
    const start = index.elements.get(spanner.start.id)
    let end = index.elements.get(spanner.end.id)
    if (!start || !end) continue
    // A slur that ends on the tied-over part of a note ends on that note: the part it
    // ties into is not played as a note of its own.
    for (let guard = 0; guard < 64 && end.event.kind === 'chord'; guard += 1) {
      const notes: (Note | UnpitchedNote)[] = end.event.notes
      const source = notes.every(note => tieSources.has(note.id))
        ? tieSources.get(notes[0]!.id)
        : undefined
      if (!source) break
      end = source
    }
    if (end.absTick <= start.absTick) continue // one note, held: nothing to slur
    const sameVoice = (a: Placed, b: Placed) =>
      a.staffId === b.staffId && a.voiceIndex === b.voiceIndex
    for (const chord of chords) {
      if (chord.absTick < start.absTick || chord.absTick > end.absTick) continue
      if (!sameVoice(chord, start) && !sameVoice(chord, end)) continue
      let position: SlurPosition = 'inner'
      if (chord.event.id === start.event.id) position = 'first'
      else if (chord.event.id === end.event.id) position = 'last'
      if (!slurPositions.has(chord.event.id)) slurPositions.set(chord.event.id, position)
    }
  }

  const pitchValue = (note: Note | UnpitchedNote) =>
    note.kind === 'note' ? midiNumber(note.pitch) : 0

  // The chords of each voice in order, to know what comes right before a note and,
  // in connected phrasing, which unslurred notes run into each other.
  const previousTicks = new Map<string, number>()
  const byVoice = new Map<string, Placed<Chord>[]>()
  for (const chord of chords) {
    const key = `${chord.staffId}:${chord.voiceIndex}`
    byVoice.set(key, [...(byVoice.get(key) ?? []), chord])
  }
  const continuesTied = (chord: Placed<Chord>) =>
    chord.event.notes.every(note => tieSources.has(note.id))
  // Written-order techniques of each chord. Phrasing is decided once for the page, so
  // markings limited to some passes count as in the first one.
  const pageTechniques = (chord: Placed<Chord>) => techniquesFor(chord, 1)
  const breaksRun = (chord: Placed<Chord>, techniques: string[]) =>
    chord.event.notes.length !== 1
    || slurPositions.has(chord.event.id)
    || chord.event.articulations.some(a =>
      a.type === ArticulationType.STACCATO || a.type === ArticulationType.STACCATISSIMO)
    || techniques.some(id => id === 'core:tremolo' || id.startsWith('core:tremolo-'))
  /** `core:connected` / `core:detached` markings switch the profile's default from there. */
  const phrasingOf = (techniques: string[]) => {
    const marked = techniques.findLast(id => id === 'core:connected' || id === 'core:detached')
    if (marked) return marked === 'core:connected' ? 'connected' : 'detached'
    return profile.defaultPhrasing ?? 'detached'
  }
  const accented = (chord: Placed<Chord>) =>
    chord.event.articulations.some(a => a.type === ArticulationType.ACCENT)

  for (const voice of byVoice.values()) {
    let run: Placed<Chord>[] = []
    let previous: Placed<Chord> | undefined
    let previousEnd = -1
    const closeRun = () => {
      run.forEach((chord, i) => {
        if (run.length < 2) return
        const position = i === 0 ? 'first' : i === run.length - 1 ? 'last' : 'inner'
        slurPositions.set(chord.event.id, position)
      })
      run = []
    }
    for (const chord of voice) {
      // A chord that only continues ties is part of the note before it.
      if (continuesTied(chord) && previous && chord.absTick === previousEnd) {
        previousEnd = chord.absTick + chord.ticks
        continue
      }
      const touches = previous !== undefined && chord.absTick === previousEnd
      if (touches) previousTicks.set(chord.event.id, previous!.ticks)
      const techniques = pageTechniques(chord)
      if (phrasingOf(techniques) === 'connected') {
        const samePitch = touches && pitchValue(previous!.event.notes[0]!)
          === pitchValue(chord.event.notes[0]!)
        const breaks = breaksRun(chord, techniques)
        if (!touches || samePitch || breaks || accented(chord)) closeRun()
        if (!breaks) run.push(chord)
      } else closeRun()
      previous = chord
      previousEnd = chord.absTick + chord.ticks
    }
    closeRun()
  }

  const writtenPhrases = new Map(slurPositions)
  /** Event id → its place in the phrase as written. */
  const phrases = new Map<string, SlurPosition>()

  // A repeated pitch cannot be slurred into: in a slur, the note before it ends the
  // phrase and the repeat starts a new one, so it is played detached, on its beat.
  const pitchesOf = (chord: Placed<Chord>) =>
    chord.event.notes.map(pitchValue).sort((a, b) => a - b).join(',')
  for (const voice of byVoice.values()) {
    let previous: Placed<Chord> | undefined
    let previousEnd = -1
    for (const chord of voice) {
      if (continuesTied(chord) && previous && chord.absTick === previousEnd) {
        previousEnd = chord.absTick + chord.ticks
        continue
      }
      const before = previous && slurPositions.get(previous.event.id)
      const here = slurPositions.get(chord.event.id)
      const joined = previous && chord.absTick === previousEnd
        && (before === 'first' || before === 'inner') && (here === 'inner' || here === 'last')
      if (joined && pitchesOf(previous!) === pitchesOf(chord)) {
        if (before === 'first') slurPositions.delete(previous!.event.id)
        else slurPositions.set(previous!.event.id, 'last')
        if (here === 'last') slurPositions.delete(chord.event.id)
        else slurPositions.set(chord.event.id, 'first')
      }
      previous = chord
      previousEnd = chord.absTick + chord.ticks
    }
  }

  // Glissandi: the chord a line starts on, with the pitches of the chord it leads to.
  const glissandi = new Map<string, number[]>()
  for (const spanner of track.trackSpanners) {
    if (spanner.kind !== 'custom-spanner' || spanner.techniqueId !== 'core:gliss') continue
    if (spanner.start.type !== 'element' || spanner.end.type !== 'element') continue
    const end = index.elements.get(spanner.end.id)
    if (end?.event.kind !== 'chord') continue
    const pitches = end.event.notes.flatMap(n => n.kind === 'note' ? [midiNumber(n.pitch)] : [])
    if (pitches.length > 0) glissandi.set(spanner.start.id, pitches)
  }

  const kitSound = (note: UnpitchedNote) =>
    track.kit?.find(entry => entry.id === note.kitEntryId)?.sound ?? note.kitEntryId

  const notes: NoteEvent[] = []
  /** Where grace notes begin ahead of a beat, per voice. */
  const earlyGraces: { staffId: string, voiceIndex: number, startTick: number, beat: number }[] = []

  const finish = (event: NoteEvent, context: NoteContext) => {
    const fullTicks = event.durationTicks
    let attack: Extract<NoteRuleAction, { op: 'rollAttack' }> | undefined
    for (const rule of profile.noteRules) {
      if (!matches(rule, event, context, tpq, ctx.tempo)) continue
      rule.actions.forEach(action => {
        if (action.op === 'rollAttack') attack = action
        act(event, action, tpq, fullTicks)
      })
    }
    override(event, overrides)
    // A roll that starts with a single stroke: the stroke is a plain note of its own.
    if (attack && event.techniques.some(id => ROLL_TECHNIQUES.has(id))) {
      const ticks = attack.qn === undefined
        ? event.durationTicks
        : Math.min(attack.qn * tpq, event.durationTicks / 2)
      notes.push({
        ...event,
        id: `${event.id}~attack`,
        techniques: event.techniques.filter(id => !ROLL_TECHNIQUES.has(id)),
        notatedTicks: ticks,
        durationTicks: ticks,
      })
      if (!attack.together && attack.qn !== undefined) {
        // The roll comes in after the stroke and still ends where it was written to.
        event.startTick += ticks
        event.durationTicks -= ticks
        event.notatedTicks -= ticks
      }
    }
    notes.push(event)
  }

  const rolls = (placed: Placed<Chord>, pass: number) => techniquesFor(placed, pass)
    .some(id => id === 'core:tremolo' || /^core:tremolo-\d$/.test(id))

  /**
   * Length of a note with everything tied to it, and the ids of the notes it covers.
   * `rollTicks`: of a tremolo, how long it lasts, up to the first tied note without one.
   */
  const tieChain = (note: Note | UnpitchedNote, placed: Placed<Chord>, playedAt: number) => {
    const sourceIds = [note.id]
    let ticks = placed.ticks
    const rolling = rolls(placed, played[playedAt]!.pass)
    let rollTicks = rolling ? ticks : 0
    let current = note
    let measure = playedAt
    for (;;) {
      // Follow the tie only into a measure that is played right after this one. With
      // several targets (a tie into both endings) that picks the ending being played;
      // with none, the note ends at this barline.
      const here = played[measure]!.info.index
      const after = played[measure + 1]?.info.index
      const targetId = tieTargets(current).find(id => {
        const targetMeasure = index.elements.get(id)?.measureIndex
        return targetMeasure === here || (targetMeasure !== undefined && targetMeasure === after)
      })
      const target = targetId === undefined ? undefined : index.elements.get(targetId)
      if (!target || target.event.kind !== 'chord') break
      const next = target.event.notes.find(n => n.id === targetId)
      if (!next) break
      if (target.measureIndex !== here) measure += 1
      sourceIds.push(next.id)
      if (rolling && rollTicks === ticks && rolls(target as Placed<Chord>, played[measure]!.pass))
        rollTicks += target.ticks
      ticks += target.ticks
      current = next
    }
    return { sourceIds, ticks, rollTicks }
  }

  /**
   * Whether a tied-to note is the continuation of a note that was just played. If its
   * tie comes from a measure that was not played right before (after a jump, or in a
   * second ending), it sounds as a new note.
   */
  const continuesTie = (note: Note | UnpitchedNote, placed: Placed, playedAt: number) => {
    const source = tieSources.get(note.id)
    if (!source) return false
    if (source.measureIndex === placed.measureIndex) return source.absTick < placed.absTick
    return played[playedAt - 1]?.info.index === source.measureIndex
  }

  played.forEach((playedMeasure, playedAt) => {
    const measureIndex = playedMeasure.info.index
    for (const placed of chords) {
      if (placed.measureIndex !== measureIndex) continue
      const chord = placed.event
      const scope = { staffId: placed.staffId, voiceIndex: placed.voiceIndex }
      const beat = playedMeasure.startTick + placed.tick
      const techniques = techniquesFor(placed, playedMeasure.pass)
      const slur = slurPositions.get(chord.id)
      const articulations = chord.articulations.map(a => a.type)
      const base = {
        kind: 'note-event' as const,
        trackId: track.id,
        ...scope,
        pass: playedMeasure.pass,
        velocity: clamp01(
          evaluateCurve(curves, 'dynamics', beat, initialDynamics, scope),
        ),
      }
      const pitchOf = (note: Note | UnpitchedNote) =>
        note.kind === 'note' ? midiNumber(note.pitch) : { sound: kitSound(note) }

      // Grace notes: each takes `ratio` of its notated length, either before the beat
      // or out of the start of the main note. They never take more than half of it.
      const graces = chord.graceNotes
      const slash = graces?.slash === true
      const graceNotes = profile.graceNotes ?? DEFAULT_GRACE_NOTES
      const graceSetup = slash ? graceNotes.acciaccatura : graceNotes.appoggiatura
      const placement = graceSetup.placement ?? (slash ? 'before-beat' : 'on-beat')
      const onBeat = placement === 'on-beat'
      let graceLengths = (graces?.chords ?? [])
        .map(graceChord => durationTicks(graceChord.duration, tpq) * graceSetup.ratio)
      let graceTotal = graceLengths.reduce((sum, length) => sum + length, 0)
      if (onBeat && graceTotal > placed.ticks / 2) {
        graceLengths = graceLengths.map(length => length * placed.ticks / 2 / graceTotal)
        graceTotal = placed.ticks / 2
      }
      const mainStart = onBeat ? beat + graceTotal : beat
      let graceStart = onBeat ? beat : beat - graceTotal
      if (!onBeat && graceTotal > 0) earlyGraces.push({ ...scope, startTick: graceStart, beat })
      graces?.chords.forEach((graceChord, i) => {
        for (const note of graceChord.notes) {
          finish({
            ...base,
            id: `${note.id}@${playedMeasure.pass}`,
            sourceIds: [note.id],
            pitch: pitchOf(note),
            startTick: graceStart,
            notatedTicks: durationTicks(graceChord.duration, tpq),
            durationTicks: graceLengths[i]!,
            techniques: [...techniques],
          }, { articulations: graceChord.articulations.map(a => a.type), grace: { slash }, beat })
        }
        graceStart += graceLengths[i]!
      })

      // Marks that are played as several notes. These are fixed behaviours for now:
      // the profile types have no way yet to describe a rule that creates notes.
      // A measured tremolo stays one note of its written length: the sample library plays
      // the repetitions (measured tremolo, measured repetitions, double tongue).
      const measured = techniques.some(id => /^core:tremolo-\d$/.test(id))
      const sounding = [
        ...techniques.filter(id => !GENERATED.has(id)),
        ...measured ? ['core:measured-tremolo'] : [],
      ]
      const rolled = techniques.includes('core:arpeggio')
        ? [...chord.notes].sort((a, b) => pitchValue(a) - pitchValue(b))
        : undefined
      const glissTo = glissandi.get(chord.id)
      const dynamicsOn = dynamicsFor(placed)
      const hairpinsOn = hairpinsFor(placed)
      const strokes = [1, 2].find(n => techniques.includes(`core:tremolo-${n}`))
      const tremolo = measured || techniques.includes('core:tremolo')
      const pair = tremoloPairs.get(chord.id)
      // Trill: the upper note sounds with the top note of the chord, as the library wants.
      const trillTo = techniques.includes('core:trill-half') ? 1
        : techniques.includes('core:trill-whole') ? 2 : undefined
      const top = Math.max(...chord.notes.map(pitchValue))

      chord.notes.forEach((note, noteIndex) => {
        if (continuesTie(note, placed, playedAt)) return
        const chain = tieChain(note, placed, playedAt)
        const letRing = note.tie !== undefined && 'letRing' in note.tie
        let start = mainStart
        let length = chain.ticks - (mainStart - beat)
        let notated = chain.ticks
        // A roll tied into notes without one stops where the tremolo stops; the rest of
        // the tie still counts as written, so that a rest is only found after it.
        if (chain.rollTicks > 0 && chain.rollTicks < chain.ticks)
          length = chain.rollTicks - (mainStart - beat)

        // Two-note tremolo: both notes start with the first and end with the second, which
        // is how a sample library is asked for a trill.
        if (pair) {
          notated = pair.end.absTick + pair.end.ticks - pair.start.absTick
          start = beat - (placed.absTick - pair.start.absTick)
          length = notated
        }

        // Arpeggio: the notes enter one after another, lowest first.
        if (rolled && rolled.length > 1) {
          const step = Math.min(tpq / 8, length / 2 / (rolled.length - 1))
          const delay = rolled.indexOf(note) * step
          start += delay
          length -= delay
        }

        const emit = (
          suffix: string, pitch: NoteEvent['pitch'], from: number, ticks: number, plain = false,
          velocity = base.velocity,
        ) => {
          const kept = plain
            ? sounding.filter(id => !ROLL_TECHNIQUES.has(id) && !/^core:tremolo/.test(id))
            : sounding
          const phrase = writtenPhrases.get(chord.id)
          if (phrase) phrases.set(`${note.id}@${playedMeasure.pass}${suffix}`, phrase)
          finish({
            ...base,
            velocity,
            id: `${note.id}@${playedMeasure.pass}${suffix}`,
            sourceIds: chain.sourceIds,
            pitch,
            startTick: from,
            notatedTicks: suffix ? ticks : notated,
            durationTicks: ticks,
            techniques: letRing ? [...kept, 'core:let-ring'] : [...kept],
            ...(slur ? { slur } : {}),
          }, {
            articulations,
            beat,
            // A note that opens a phrase is not joined to the note before it.
            ...(previousTicks.has(chord.id) && slur !== 'first'
              ? { previousTicks: previousTicks.get(chord.id)! }
              : {}),
            // A dynamic belongs to the first of the notes a written note becomes.
            ...(dynamicsOn.length > 0 && !suffix ? { dynamics: dynamicsOn } : {}),
            ...(hairpinsOn.length > 0 ? { hairpins: hairpinsOn } : {}),
          })
        }

        // Tremolo written out, for a profile that asks for it (mallets cannot roll by
        // themselves): measured by its strokes, unmeasured at a fixed speed.
        const expand = profile.expandTremolo
        if (tremolo && expand && (strokes !== undefined || expand.unmeasuredMs) && !pair) {
          let unit: number
          if (strokes !== undefined) {
            // A stroke counts like a flag: one is eighths on a quarter, half or whole note,
            // sixteenths on an eighth.
            const plain = durationTicks({ type: chord.duration.type, dots: 0 }, tpq)
            const scale = placed.ticks / durationTicks(chord.duration, tpq) // inside a tuplet
            unit = Math.min(plain, tpq) * scale / 2 ** strokes
          } else {
            const ms = ctx.tempo.tickToMs(start + length) - ctx.tempo.tickToMs(start)
            unit = length / Math.max(1, Math.round(ms / expand.unmeasuredMs!))
          }
          const count = Math.round(length / unit)
          if (count > 1) {
            // Plain strokes: the patch is not asked to roll them. Each takes its velocity
            // from the dynamics where it starts, so the strokes follow a hairpin.
            for (let i = 0; i < count; i += 1) {
              const from = start + i * unit
              const velocity = i === 0 ? base.velocity
                : clamp01(evaluateCurve(curves, 'dynamics', from, initialDynamics, scope))
              emit(i === 0 ? '' : `~${i}`, pitchOf(note), from, unit, true, velocity)
            }

            return
          }
        }

        // Glissando: the second half of the note runs in semitones to the next note.
        const target = glissTo?.[Math.min(noteIndex, glissTo.length - 1)]
        const own = pitchOf(note)
        if (target !== undefined && typeof own === 'number' && Math.abs(target - own) > 1) {
          const direction = Math.sign(target - own)
          const between = Math.abs(target - own) - 1
          // Never faster than a 64th note: skip pitches when the gap is too wide.
          const steps = Math.max(1, Math.min(between, Math.floor(length / 2 / (tpq / 16))))
          const run = length / 2 / steps
          emit('', own, start, length / 2)
          for (let i = 0; i < steps; i += 1) {
            const pitch = own + direction * Math.round((i + 1) * between / steps)
            emit(`~g${i + 1}`, pitch, start + length / 2 + i * run, run)
          }
          return
        }

        emit('', own, start, length)
        if (trillTo !== undefined && typeof own === 'number' && pitchValue(note) === top)
          emit('~tr', own + trillTo, start, length)
      })
    }
  })

  // Grace notes before the beat take their time from the note before them: in the same
  // voice, whatever is still sounding when they begin ends there.
  for (const grace of earlyGraces) {
    for (const note of notes) {
      if (note.staffId !== grace.staffId || note.voiceIndex !== grace.voiceIndex) continue
      const end = note.startTick + note.durationTicks
      if (note.startTick < grace.startTick && end > grace.startTick && end <= grace.beat)
        note.durationTicks = grace.startTick - note.startTick
    }
  }

  return { notes: notes.sort((a, b) => a.startTick - b.startTick), phrases }
}

/** MusicData → MusicEvent[]: repeats unrolled, profiles and overrides applied. */
export const buildMusicEvents = (music: MusicData, profiles: PlaybackSetup): EventResult => {
  const timeline = measureTimeline(music)
  const played = expandRepeats(music, timeline)
  const index = indexScore(music, timeline)
  const tempo = buildTempoMap(music, played, index, timeline)
  const warnings: string[] = []
  const events: MusicEvent[] = []

  for (const track of music.tracks) {
    const assignment = profiles.trackProfiles[track.id]
    if (!assignment) {
      warnings.push(`track ${track.id}: no profiles assigned, skipped`)
      continue
    }
    const profile = resolveInterpretationProfile(
      profiles.interpretationProfiles, assignment.interpretationProfileId,
    )
    const ctx: TrackContext = {
      music, profiles, timeline, played, index, track, profile, tempo, warnings,
    }
    const curves = buildCurves(ctx)
    const { notes, phrases } = buildNotes(ctx, curves)
    events.push(...curves, ...buildSwells(ctx, notes, curves, phrases), ...notes)
  }
  return { played, tempo, events, warnings }
}
