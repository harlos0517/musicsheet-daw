import type { CurveEvent, NoteEvent, SlurPosition } from '@/core/types/MusicEvent'
import type { TrackContext } from './buildEvents'

/**
 * Swells: the small crescendo into and diminuendo out of a held note that players add
 * without it being written. A long single note gets one of its own; a slur gets one for
 * the whole phrase, rising over its first note and falling over its last.
 *
 * They are `combine: 'scale'` segments on the dynamics curve, so they shape the level
 * written there and leave velocities alone. Where the page itself changes the dynamic
 * (a hairpin, or a new dynamic inside the note), the page wins: the rise or the fall that
 * would overlap it is left out.
 */
export const buildSwells = (
  ctx: TrackContext, notes: NoteEvent[], curves: CurveEvent[],
  phrases: Map<string, SlurPosition> = new Map(),
): CurveEvent[] => {
  // The phrase as written: a slur that playing splits at a repeated pitch is still one.
  const slurOf = (note: NoteEvent) => phrases.get(note.id) ?? note.slur
  const settings = ctx.profile.swell
  if (!settings) return []
  const { tempo, track } = ctx
  // Rounded, so a note exactly at the limit is not counted as longer by a rounding error.
  const lengthMs = (note: NoteEvent) => Math.round(
    (tempo.tickToMs(note.startTick + note.durationTicks) - tempo.tickToMs(note.startTick)) * 1000,
  ) / 1000
  /** Written length in real time: what tells a slow slur note from a fast one. */
  const notatedMs = (note: NoteEvent) => Math.round(
    (tempo.tickToMs(note.startTick + note.notatedTicks) - tempo.tickToMs(note.startTick)) * 1000,
  ) / 1000
  const slurMinMs = settings.slurMinMs ?? settings.minMs
  const swells: CurveEvent[] = []

  // Written dynamics: the base layer of the curve, without accents or other swells.
  const written = curves.filter(curve => curve.parameter === 'dynamics'
    && (curve.combine === undefined || curve.combine === 'replace'))
  /** Whether the page changes this voice's dynamic between `from` and `to`. */
  const changesWritten = (note: NoteEvent, from: number, to: number) => written.some(curve => {
    if (curve.appliesTo && !curve.appliesTo.some(scope => scope.staffId === note.staffId
      && (scope.voiceIndex === undefined || scope.voiceIndex === note.voiceIndex))) return false
    const end = curve.startTick + curve.durationTicks
    // A dynamic on the note's own first beat is its level, not a change.
    return curve.durationTicks > 0
      ? curve.startTick < to && end > from
      : curve.startTick > from && curve.startTick < to
  })

  /** Where a swell would rise and fall, and which of the two the page leaves room for. */
  const shapeOf = (first: NoteEvent, last: NoteEvent, ratio: number, slur: boolean) => {
    const start = first.startTick
    const end = last.startTick + last.durationTicks
    const rise = first.durationTicks * ratio
    const fall = last.durationTicks * ratio
    // A slur rises only out of a slow first note and falls only into a slow last one:
    // the same line that sets a fast run (marcato) apart from a slow slur (sustain).
    const slow = (note: NoteEvent) => !slur || notatedMs(note) > slurMinMs
    return {
      start, end, rise, fall,
      rises: slow(first) && !changesWritten(first, start, start + rise),
      falls: slow(last) && !changesWritten(last, end - fall, end),
    }
  }

  /** `resetTick`: where the written level comes back, or none when another swell follows. */
  const swell = (
    first: NoteEvent, shape: ReturnType<typeof shapeOf>, resetTick?: number,
  ) => {
    const { start, end, rise, fall } = shape
    const sourceId = first.sourceIds[0]!
    const common = {
      kind: 'curve-event' as const,
      sourceIds: [sourceId],
      trackId: track.id,
      pass: first.pass,
      parameter: 'dynamics' as const,
      combine: 'scale' as const,
      appliesTo: [{ staffId: first.staffId, voiceIndex: first.voiceIndex }],
    }
    const low = settings.startLevel
    const riseShape = settings.rise ?? {}
    const fallShape = settings.fall ?? {}
    // Indexes stay 0 rise, 1 fall, 2 reset, also when one of them is left out.
    const segments = [
      shape.rises && { startTick: start, durationTicks: rise, fromValue: low, toValue: 1,
        ...riseShape },
      shape.falls && { startTick: end - fall, durationTicks: fall, fromValue: 1, toValue: low,
        ...fallShape },
      // Back to the written level at the next note, so that it is not left lowered and
      // the silence before it does not jump up.
      shape.falls && resetTick !== undefined
        && { startTick: resetTick, durationTicks: 0, fromValue: low, toValue: 1 },
    ]
    segments.forEach((segment, segmentIndex) => {
      if (!segment) return
      swells.push({
        ...common,
        ...segment,
        id: `${sourceId}@${first.pass}#swell${segmentIndex}`,
        segmentIndex,
      })
    })
  }

  // Notes of each voice in time order. One swell per point in time: the notes of a
  // chord share it. Notes the engine made out of one written note (tremolo strokes,
  // glissando steps) are left out.
  const voices = new Map<string, NoteEvent[]>()
  for (const note of notes) {
    if (note.id.includes('~')) continue
    const key = `${note.staffId}:${note.voiceIndex}`
    const list = voices.get(key) ?? []
    if (list.at(-1)?.startTick !== note.startTick) list.push(note)
    voices.set(key, list)
  }

  for (const voice of voices.values()) {
    voice.sort((a, b) => a.startTick - b.startTick)
    // First and last note index of each swell, with its ratio.
    const planned: { first: number, last: number, ratio: number, slur: boolean }[] = []
    let phraseStart: number | undefined
    voice.forEach((note, i) => {
      if (slurOf(note) === 'first') {
        phraseStart = i
        return
      }
      if (slurOf(note) === 'inner') return
      if (slurOf(note) === 'last') {
        if (phraseStart !== undefined)
          planned.push({ first: phraseStart, last: i, ratio: settings.slurRatio, slur: true })

        phraseStart = undefined
        return
      }
      if (lengthMs(note) > settings.minMs)
        planned.push({ first: i, last: i, ratio: settings.noteRatio, slur: false })
    })
    const shapes = planned.map(({ first, last, ratio, slur }) =>
      shapeOf(voice[first]!, voice[last]!, ratio, slur))
    // Notes that start with a rise from the low level: no reset is needed before them.
    const risesAt = new Set(planned.flatMap((plan, i) => shapes[i]!.rises ? [plan.first] : []))
    planned.forEach(({ first, last }, i) => {
      const next = voice[last + 1]
      const end = voice[last]!.startTick + voice[last]!.durationTicks
      const resetTick = !next ? end : risesAt.has(last + 1) ? undefined : next.startTick
      swell(voice[first]!, shapes[i]!, resetTick)
    })
  }
  return swells
}
