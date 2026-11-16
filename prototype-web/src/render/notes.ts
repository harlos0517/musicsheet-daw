/**
 * Converts one staff measure of MusicData into VexFlow notes and voices.
 * Pure conversion: no layout decisions beyond what VexFlow needs to draw a measure.
 */
import {
  Beam,
  ClefNote,
  Dot,
  Fraction,
  GhostNote,
  GraceNote,
  GraceNoteGroup,
  Modifier,
  StaveNote,
  Stem,
  Accidental as VFAccidental,
  Articulation as VFArticulation,
  type Note as VFNote,
  Tuplet as VFTuplet,
  Voice as VFVoice,
} from 'vexflow/bravura'

import type {
  Chord,
  Clef,
  Duration,
  KitEntry,
  Pitch,
  StaffMeasure,
  TimeSignature,
  VoiceEvent,
} from '@/core/types/MusicData'
import { ArticulationType, ClefType } from '@/core/types/MusicData'
import {
  STEPS,
  type Transposition,
  diatonicIndex,
  keyAlter,
  transposePitch,
} from '@/core/validation/pitch'
import { durationTicks, timeVoice } from '@/core/validation/time'

export interface StaffMeasureContext {
  tpq: number
  lines: number
  /** Key in effect at the start of the measure, already transposed to written pitch. */
  writtenKey: number
  measureStartTick: number
  lengthTicks: number
  timeSig: TimeSignature
  /** Clef in effect at an absolute tick. */
  clefAt: (tick: number) => Clef
  /** 8va shift (in octaves) in effect at an absolute tick. */
  ottavaAt: (tick: number) => number
  transposition?: Transposition | undefined
  kit?: KitEntry[] | undefined
  /** IDs of notes that some other note ties into. */
  tieTargets: Set<string>
}

/** A rendered element that spanners and ties can attach to. */
export interface NoteRef {
  note: VFNote
  /** Key index inside the VexFlow note, for individual notes of a chord. */
  index?: number
}

export interface BuiltStaffMeasure {
  voices: VFVoice[]
  beams: Beam[]
  tuplets: VFTuplet[]
  /** Element ID → VexFlow note, for chords, rests and individual notes. */
  refs: Map<string, NoteRef>
  /** Every non-grace note with its absolute start tick, for placing things by time. */
  points: { tick: number, note: VFNote }[]
  /** A clef change placed just before the barline. */
  endClef?: Clef
}

/* ---------- small mappings ---------- */

const VF_DURATIONS: Record<number, string> = {
  4: '1/2', 3: '1/2', 2: '1', 1: '2', 0: '4',
  [-1]: '8', [-2]: '16', [-3]: '32', [-4]: '64', [-5]: '128',
}
export const vfDuration = (duration: Duration): string => VF_DURATIONS[duration.type] ?? '4'

const ARTICULATION_CODES: Record<ArticulationType, string> = {
  [ArticulationType.ACCENT]: 'a>',
  [ArticulationType.TENUTO]: 'a-',
  [ArticulationType.STACCATO]: 'a.',
  [ArticulationType.STACCATISSIMO]: 'av',
}

const ACCIDENTAL_CODES: Record<number, string> = { [-2]: 'bb', [-1]: 'b', 0: 'n', 1: '#', 2: '##' }

const NOTEHEAD_CODES: Record<KitEntry['notehead'], string | undefined> = {
  'normal': undefined, 'x': 'x', 'circle-x': 'x3', 'triangle': 't', 'diamond': 'd',
}

/** VexFlow clef name and its 8va/8vb annotation. Clef lines count from the bottom (1–5). */
export const vfClef = (clef: Clef): { name: string, annotation?: string } => {
  const names: Record<ClefType, Record<number, string>> = {
    [ClefType.G]: { 1: 'french', 2: 'treble' },
    [ClefType.F]: { 3: 'baritone-f', 4: 'bass', 5: 'subbass' },
    [ClefType.C]: { 1: 'soprano', 2: 'mezzo-soprano', 3: 'alto', 4: 'tenor', 5: 'baritone-c' },
    [ClefType.PERCUSSION]: {},
  }
  const name = clef.type === ClefType.PERCUSSION
    ? 'percussion'
    : names[clef.type][clef.line] ?? 'treble'
  if (clef.octaveChange > 0) return { name, annotation: '8va' }
  if (clef.octaveChange < 0) return { name, annotation: '8vb' }
  return { name }
}

/** Diatonic index of the clef's middle staff line, in the clef's own (display) coordinates. */
const middleLineIndex = (clef: Clef): number => {
  if (clef.type === ClefType.PERCUSSION)
    return diatonicIndex({ step: STEPS[6]!, alter: 0, octave: 4 }) // B4
  // G4, F3, C4: the pitch each clef sign marks
  const reference = { [ClefType.G]: 32, [ClefType.F]: 24, [ClefType.C]: 28 }[clef.type]
  return reference + (3 - clef.line) * 2
}

const keyFor = (index: number, notehead?: string): string => {
  const step = STEPS[((index % 7) + 7) % 7]!
  const key = `${step}/${Math.floor(index / 7)}`
  return notehead ? `${key}/${notehead}` : key
}

/** Beat length used for automatic beaming. */
const beamBeatTicks = (timeSig: TimeSignature, tpq: number): number => {
  if (timeSig.unit === 8 && timeSig.beats % 3 === 0) return tpq * 1.5
  if (timeSig.unit <= 2) return tpq * 2
  return tpq
}

/* ---------- accidentals ---------- */

type AccidentalMark = { code: string, cautionary: boolean }

/**
 * Decides which accidentals to print in a measure: key signature, earlier accidentals
 * on the same line and space in the bar, and the per-note override.
 */
const computeAccidentals = (
  staffMeasure: StaffMeasure, ctx: StaffMeasureContext, written: (p: Pitch) => Pitch,
): Map<string, AccidentalMark> => {
  type Item = { tick: number, order: number, chord: Omit<Chord, 'graceNotes'> }
  const items: Item[] = []
  staffMeasure.voices.forEach((voice, v) => {
    for (const { event, tick } of timeVoice(voice.events, ctx.tpq)) {
      if (event.kind !== 'chord') continue
      event.graceNotes?.chords.forEach((g, i) =>
        items.push({ tick, order: v * 100 + i - 50, chord: g }))
      items.push({ tick, order: v * 100, chord: event })
    }
  })
  items.sort((a, b) => a.tick - b.tick || a.order - b.order)

  const state = new Map<string, number>()
  const result = new Map<string, AccidentalMark>()
  for (const { chord } of items) {
    for (const n of chord.notes) {
      if (n.kind !== 'note') continue
      const pitch = written(n.pitch)
      const line = `${pitch.step}${pitch.octave}`
      const expected = state.get(line) ?? keyAlter(ctx.writtenKey, pitch.step)
      const tiedInto = ctx.tieTargets.has(n.id)
      const forced = n.accidentalDisplay === 'show' || n.accidentalDisplay === 'cautionary'
      const differs = pitch.alter !== expected
      const show = forced || (n.accidentalDisplay !== 'hide' && !tiedInto && differs)
      if (show) {
        result.set(n.id, {
          code: ACCIDENTAL_CODES[pitch.alter] ?? 'n',
          cautionary: n.accidentalDisplay === 'cautionary',
        })
      }
      if (!tiedInto) state.set(line, pitch.alter)
    }
  }
  return result
}

/* ---------- building ---------- */

export const buildStaffMeasure = (
  staffMeasure: StaffMeasure,
  ctx: StaffMeasureContext,
): BuiltStaffMeasure => {
  const { tpq } = ctx
  const written = (p: Pitch) => ctx.transposition ? transposePitch(p, ctx.transposition, -1) : p
  const accidentals = computeAccidentals(staffMeasure, ctx, written)
  const multiVoice = staffMeasure.voices.length > 1
  const refs = new Map<string, NoteRef>()
  const points: BuiltStaffMeasure['points'] = []
  const beams: Beam[] = []
  const tuplets: VFTuplet[] = []

  const midClefs = staffMeasure.clefs.filter(c => c.tick > 0 && c.tick < ctx.lengthTicks)
  const endClef = staffMeasure.clefs.find(c => c.tick === ctx.lengthTicks)?.clef

  /** Keys (sorted low → high) for a chord, with the original note for each key. */
  const chordKeys = (chord: Omit<Chord, 'graceNotes'>, tick: number) => {
    const clef = ctx.clefAt(tick)
    const shift = 7 * clef.octaveChange + 7 * ctx.ottavaAt(tick)
    const keyed = chord.notes.map(n => {
      if (n.kind === 'note')
        return { index: diatonicIndex(written(n.pitch)) - shift, note: n, head: undefined }
      const entry = ctx.kit?.find(k => k.id === n.kitEntryId)
      // F5: the top line, in treble-clef positions
      const top = diatonicIndex({ step: STEPS[3]!, alter: 0, octave: 5 })
      const middle = top - (ctx.lines - 1)
      return {
        index: middle + (entry?.displayPitch ?? 0),
        note: n,
        head: entry ? NOTEHEAD_CODES[entry.notehead] : undefined,
      }
    })
    keyed.sort((a, b) => a.index - b.index)
    return keyed
  }

  const headCode = (code: string | undefined, duration: Duration) => {
    if (!code) return undefined
    if (code === 'x3') return 'x3'
    return `${code}${duration.type >= 2 ? 0 : duration.type === 1 ? 1 : 2}`
  }

  const stemFor = (voiceIndex: number) =>
    multiVoice ? { stemDirection: voiceIndex % 2 === 0 ? Stem.UP : Stem.DOWN } : { autoStem: true }

  const decorate = (
    note: StaveNote,
    chord: Omit<Chord, 'graceNotes'>,
    keyed: ReturnType<typeof chordKeys>,
  ) => {
    keyed.forEach(({ note: n }, i) => {
      const mark = accidentals.get(n.id)
      if (!mark) return
      const accidental = new VFAccidental(mark.code)
      if (mark.cautionary) accidental.setAsCautionary()
      note.addModifier(accidental, i)
    })
    for (let d = 0; d < chord.duration.dots; d++) Dot.buildAndAttach([note], { all: true })
  }

  const buildChord = (chord: Chord, tick: number, voiceIndex: number): StaveNote => {
    const keyed = chordKeys(chord, tick)
    const clefName = vfClef(ctx.clefAt(tick)).name
    const note = new StaveNote({
      keys: keyed.map(k => keyFor(k.index, headCode(k.head, chord.duration))),
      duration: vfDuration(chord.duration),
      dots: chord.duration.dots,
      clef: clefName,
      ...stemFor(voiceIndex),
      ...(chord.duration.type === 4 ? { durationOverride: new Fraction(4, 1) } : {}),
    })
    decorate(note, chord, keyed)

    const stemUp = note.getStemDirection() === Stem.UP
    for (const a of chord.articulations) {
      const articulation = new VFArticulation(ARTICULATION_CODES[a.type])
      articulation.setPosition(stemUp ? Modifier.Position.BELOW : Modifier.Position.ABOVE)
      note.addModifier(articulation, 0)
    }

    if (chord.graceNotes && chord.graceNotes.chords.length > 0) {
      const graces = chord.graceNotes.chords.map(g => {
        const gKeyed = chordKeys(g, tick)
        const grace = new GraceNote({
          keys: gKeyed.map(k => keyFor(k.index, headCode(k.head, g.duration))),
          duration: vfDuration(g.duration),
          dots: g.duration.dots,
          clef: clefName,
          slash: chord.graceNotes!.slash ?? false,
        })
        decorate(grace, g, gKeyed)
        gKeyed.forEach(({ note: n }, i) => refs.set(n.id, { note: grace, index: i }))
        refs.set(g.id, { note: grace })
        return grace
      })
      const group = new GraceNoteGroup(graces, false)
      if (graces.length > 1) group.beamNotes()
      note.addModifier(group, 0)
    }

    keyed.forEach(({ note: n }, i) => refs.set(n.id, { note, index: i }))
    refs.set(chord.id, { note })
    return note
  }

  const buildRest = (
    event: Extract<VoiceEvent, { kind: 'rest' }>,
    tick: number,
    voiceIndex: number,
  ): StaveNote => {
    const clef = ctx.clefAt(tick)
    const offset = multiVoice ? (voiceIndex % 2 === 0 ? 4 : -4) : 0
    const key = keyFor(middleLineIndex(clef) + offset + (event.fullMeasure ? 2 : 0))
    const note = event.fullMeasure
      ? new StaveNote({
        keys: [key], duration: '1', type: 'r', clef: vfClef(clef).name, alignCenter: true,
        durationOverride: new Fraction(ctx.lengthTicks, 4 * tpq),
      })
      : new StaveNote({
        keys: [key],
        duration: vfDuration(event.duration),
        dots: event.duration.dots,
        type: 'r',
        clef: vfClef(clef).name,
      })
    if (!event.fullMeasure)
      for (let d = 0; d < event.duration.dots; d++) Dot.buildAndAttach([note], { all: true })
    refs.set(event.id, { note })
    return note
  }

  const voices = staffMeasure.voices.map((voice, voiceIndex) => {
    const tickables: VFNote[] = []
    const beamable: { note: StaveNote, tick: number, mode: Chord['beam'] }[] = []
    const breakBeam = () =>
      beamable.push({ note: null as unknown as StaveNote, tick: -1, mode: 'none' })
    let clefIndex = 0

    /** Builds events in order; returns the leaf notes created (for tuplets). */
    const build = (events: VoiceEvent[], start: number, scale: number): VFNote[] => {
      const leaves: VFNote[] = []
      let tick = start
      for (const event of events) {
        const absolute = ctx.measureStartTick + tick
        // Mid-measure clef changes go into the first voice, before the event they precede.
        while (voiceIndex === 0 && midClefs[clefIndex] && midClefs[clefIndex]!.tick <= tick) {
          const { name, annotation } = vfClef(midClefs[clefIndex]!.clef)
          tickables.push(new ClefNote(name, 'small', annotation))
          clefIndex += 1
        }
        if (event.kind === 'tuplet') {
          const inner = build(event.events, tick, scale * event.normal / event.actual)
          const tuplet = new VFTuplet(inner, {
            numNotes: event.actual,
            notesOccupied: event.normal,
          })
          tuplets.push(tuplet)
          leaves.push(...inner)
          tick += durationTicks(event.unit, tpq) * event.normal * scale
          continue
        }
        let note: VFNote
        if (event.kind === 'space') {
          note = new GhostNote({ duration: vfDuration(event.duration), dots: event.duration.dots })
          breakBeam()
        } else if (event.kind === 'rest') {
          note = buildRest(event, absolute, voiceIndex)
          if (event.beam === 'join-previous') {
            // a rest inside a beamed group: keep the group open
          } else breakBeam()
        } else {
          const staveNote = buildChord(event, absolute, voiceIndex)
          note = staveNote
          if (event.duration.type <= -1 && event.beam !== 'none')
            beamable.push({ note: staveNote, tick, mode: event.beam })
          else breakBeam()
        }
        tickables.push(note)
        leaves.push(note)
        points.push({ tick: absolute, note })
        tick += durationTicks(event.duration, tpq) * scale
      }
      return leaves
    }
    build(voice.events, 0, 1)

    // Beams: group by beat unless the data says otherwise.
    const beat = beamBeatTicks(ctx.timeSig, tpq)
    let group: StaveNote[] = []
    let groupBeat = -1
    const flush = () => {
      if (group.length > 1) beams.push(new Beam(group, !multiVoice))
      group = []
    }
    for (const { note, tick, mode } of beamable) {
      if (!note) {
        flush()
        continue
      }
      const noteBeat = Math.floor(tick / beat)
      if (mode === 'join-previous' && group.length > 0) group.push(note)
      else if (mode === 'break-before' || noteBeat !== groupBeat) {
        flush()
        group = [note]
      } else group.push(note)
      groupBeat = noteBeat
    }
    flush()

    // Counted in 128th notes so pickups and odd lengths fit.
    const numBeats = Math.max(1, Math.round(ctx.lengthTicks * 32 / tpq))
    const vfVoice = new VFVoice({ numBeats, beatValue: 128 })
    vfVoice.setMode(VFVoice.Mode.SOFT)
    vfVoice.addTickables(tickables)
    return vfVoice
  })

  return { voices, beams, tuplets, refs, points, ...(endClef ? { endClef } : {}) }
}
