import type { NoteEventOverride } from '@/core/types/EventOverride'
import type InterpretationProfile from '@/core/types/InterpretationProfile'
import type { NoteRule, NoteRuleAction } from '@/core/types/InterpretationProfile'
import type MusicData from '@/core/types/MusicData'
import type {
  ArticulationType, Chord, Note, Track, UnpitchedNote,
} from '@/core/types/MusicData'
import type {
  CurveEvent, MusicEvent, NoteEvent, SlurPosition,
} from '@/core/types/MusicEvent'
import { midiNumber } from '@/core/validation/pitch'
import type { PlayedMeasure } from '@/core/validation/repeats'
import { expandRepeats } from '@/core/validation/repeats'
import { tieTargets } from '@/core/validation/ties'
import type { MeasureInfo } from '@/core/validation/time'
import { durationTicks, measureTimeline } from '@/core/validation/time'
import type { PlaybackSetup } from '../types/PlaybackSetup'
import { buildCurves } from './buildCurves'
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
  warnings: string[]
}

export interface EventResult {
  played: PlayedMeasure[]
  events: MusicEvent[]
  warnings: string[]
}

/** What a rule can match on besides the event itself. */
interface NoteContext {
  articulations: ArticulationType[]
  grace?: { slash: boolean }
}

const matches = (
  rule: NoteRule, event: NoteEvent, context: NoteContext, tpq: number,
): boolean => {
  const { match, when } = rule
  const notatedQn = event.notatedTicks / tpq
  if (when?.maxNotatedQn !== undefined && notatedQn > when.maxNotatedQn) return false
  if (when?.minNotatedQn !== undefined && notatedQn < when.minNotatedQn) return false
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

const buildNotes = (ctx: TrackContext, curves: CurveEvent[]): NoteEvent[] => {
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
    const end = index.elements.get(spanner.end.id)
    if (!start || !end) continue
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

  const kitSound = (note: UnpitchedNote) =>
    track.kit?.find(entry => entry.id === note.kitEntryId)?.sound ?? note.kitEntryId

  const notes: NoteEvent[] = []
  /** Where grace notes begin ahead of a beat, per voice. */
  const earlyGraces: { staffId: string, voiceIndex: number, startTick: number, beat: number }[] = []

  const finish = (event: NoteEvent, context: NoteContext) => {
    const fullTicks = event.durationTicks
    for (const rule of profile.noteRules) {
      if (!matches(rule, event, context, tpq)) continue
      rule.actions.forEach(action => act(event, action, tpq, fullTicks))
    }
    override(event, overrides)
    notes.push(event)
  }

  /** Length of a note with everything tied to it, and the ids of the notes it covers. */
  const tieChain = (note: Note | UnpitchedNote, placed: Placed, playedAt: number) => {
    const sourceIds = [note.id]
    let ticks = placed.ticks
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
      ticks += target.ticks
      current = next
    }
    return { sourceIds, ticks }
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
      const graceSetup = slash
        ? { placement: 'before' as const, ...graceNotes.acciaccatura }
        : { placement: 'after' as const, ...graceNotes.appoggiatura }
      const onBeat = graceSetup.placement === 'after'
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
          }, { articulations: graceChord.articulations.map(a => a.type), grace: { slash } })
        }
        graceStart += graceLengths[i]!
      })

      for (const note of chord.notes) {
        if (continuesTie(note, placed, playedAt)) continue
        const chain = tieChain(note, placed, playedAt)
        const letRing = note.tie !== undefined && 'letRing' in note.tie
        finish({
          ...base,
          id: `${note.id}@${playedMeasure.pass}`,
          sourceIds: chain.sourceIds,
          pitch: pitchOf(note),
          startTick: mainStart,
          notatedTicks: chain.ticks,
          durationTicks: chain.ticks - (mainStart - beat),
          techniques: letRing ? [...techniques, 'core:let-ring'] : [...techniques],
          ...(slur ? { slur } : {}),
        }, { articulations })
      }
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

  return notes.sort((a, b) => a.startTick - b.startTick)
}

/** MusicData → MusicEvent[]: repeats unrolled, profiles and overrides applied. */
export const buildMusicEvents = (music: MusicData, profiles: PlaybackSetup): EventResult => {
  const timeline = measureTimeline(music)
  const played = expandRepeats(music, timeline)
  const index = indexScore(music, timeline)
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
      music, profiles, timeline, played, index, track, profile, warnings,
    }
    const curves = buildCurves(ctx)
    events.push(...curves, ...buildNotes(ctx, curves))
  }
  return { played, events, warnings }
}
