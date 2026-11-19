/**
 * MusicXML (score-partwise) → MusicData.
 *
 * What MusicData cannot hold yet is left out and counted in `skipped`, so a run over a
 * real score shows where the model is still short. Pitches are stored sounding: MusicXML
 * writes them as notated for the instrument, with the transposition beside them.
 */
import type MusicData from '@/core/types/MusicData'
import type {
  Attachment,
  Chord,
  Clef,
  Duration,
  KitEntry,
  Measure,
  Note,
  Pitch,
  Rest,
  Space,
  Staff,
  StaffMeasure,
  Tempo,
  Track,
  TrackSpanner,
  UnpitchedNote,
  VoiceEvent,
  Volta,
} from '@/core/types/MusicData'
import {
  ArticulationType, BarlineStyle, ClefType, DurationType, DynamicType, Step,
} from '@/core/types/MusicData'
import {
  diatonicIndex, keyAlter, midiNumber, STEPS, transposeKey, transposePitch,
} from '@/core/validation/pitch'
import type { Transposition } from '@/core/validation/pitch'
import { durationTicks } from '@/core/validation/time'
import type { XmlNode } from './xml'
import { child, childrenOf, numberOf, parseXml, textOf } from './xml'

export interface ImportResult {
  music: MusicData
  /** For each percussion track: kit sound → the MIDI note the file suggests for it. */
  kitNotes: Record<string, Record<string, number>>
  /** What was left out or approximated, with how often. */
  skipped: Record<string, number>
}

/** Layout and drawing detail that MusicData derives or does not store. */
const SKIPPED_ELEMENTS = new Set([
  'print', 'defaults', 'credit', 'appearance', 'supports', 'stem', 'beam', 'accidental',
  'notehead', 'midi-device', 'staff-layout', 'part-name-display', 'part-abbreviation-display',
])

const NOTE_TYPES: Record<string, DurationType> = {
  'long': DurationType.LONGA,
  'breve': DurationType.DOUBLE,
  'whole': DurationType.WHOLE,
  'half': DurationType.HALF,
  'quarter': DurationType.QUARTER,
  'eighth': DurationType._8TH,
  '16th': DurationType._16TH,
  '32nd': DurationType._32ND,
  '64th': DurationType._64TH,
  '128th': DurationType._128TH,
}

const DYNAMICS: Record<string, DynamicType> = {
  pppp: DynamicType.PPPP,
  ppp: DynamicType.PPP,
  pp: DynamicType.PP,
  p: DynamicType.P,
  mp: DynamicType.MP,
  mf: DynamicType.MF,
  f: DynamicType.F,
  ff: DynamicType.FF,
  fff: DynamicType.FFF,
  ffff: DynamicType.FFFF,
  sfz: DynamicType.SFZ,
  fp: DynamicType.FP,
  sf: DynamicType.SF,
  rfz: DynamicType.RFZ,
}
/** Dynamics the model has no type for, with the nearest one it has. */
const DYNAMIC_SUBSTITUTES: Record<string, DynamicType> = {
  sfp: DynamicType.FP,
  sfpp: DynamicType.FP,
  sffz: DynamicType.SFZ,
  fz: DynamicType.SF,
  rf: DynamicType.RFZ,
}

const ARTICULATIONS: Record<string, ArticulationType[]> = {
  'staccato': [ArticulationType.STACCATO],
  'staccatissimo': [ArticulationType.STACCATISSIMO],
  'spiccato': [ArticulationType.STACCATISSIMO],
  'tenuto': [ArticulationType.TENUTO],
  'accent': [ArticulationType.ACCENT],
  'detached-legato': [ArticulationType.TENUTO, ArticulationType.STACCATO],
}

/** Words in the score that mean a playing technique. Anything else is left out. */
const TECHNIQUE_WORDS: [RegExp, string][] = [
  [/^pizz/i, 'core:pizz'],
  [/^arco/i, 'core:arco'],
  [/^con sord/i, 'core:con-sord'],
  [/^senza sord/i, 'core:senza-sord'],
  [/^sul pont/i, 'core:sul-pont'],
  [/^sul tasto/i, 'core:sul-tasto'],
  [/^ord/i, 'core:ord'],
  // Character words that say how unslurred notes are joined, wherever they appear in the
  // text ("molto cantabile", "dolce e legato"). "non legato" is tested before "legato".
  [/\b(non[ -]?legato|maestoso|marc(\.|ato\b)|d[ée]tach[ée]|secco)/i, 'core:detached'],
  [/\b(cantabile|espr(\.|ess)|dolce|legato|cantando)/i, 'core:connected'],
]

const CLEFS: Record<string, ClefType> = {
  G: ClefType.G, F: ClefType.F, C: ClefType.C, percussion: ClefType.PERCUSSION,
}

const BARLINES: Record<string, BarlineStyle> = {
  'light-light': BarlineStyle.DOUBLE,
  'light-heavy': BarlineStyle.FINAL,
  'dashed': BarlineStyle.DASHED,
  'dotted': BarlineStyle.DASHED,
}

const ACCIDENTAL_MARKS: Record<string, number> = {
  'sharp': 1, 'natural': 0, 'flat': -1, 'double-sharp': 2, 'sharp-sharp': 2, 'flat-flat': -2,
}

/**
 * Semitones from a trilled note (written pitch) up to the note it trills with: the next
 * step, altered by an accidental on the trill, else by one earlier in the bar, else by the
 * key signature.
 */
const trillInterval = (
  written: Pitch, ornaments: XmlNode[], barAlters: Map<string, number>, fifths: number,
): number => {
  const index = diatonicIndex(written) + 1
  const step = STEPS[index % 7]!
  const octave = Math.floor(index / 7)
  const mark = ornaments.find(o => o.name === 'accidental-mark')
  const alter = (mark && ACCIDENTAL_MARKS[mark.text.trim()])
    ?? barAlters.get(`${step}${octave}`)
    ?? keyAlter(fifths, step)
  return midiNumber({ step, alter, octave }) - midiNumber(written)
}

const gcd = (a: number, b: number): number => b === 0 ? a : gcd(b, a % b)
const lcm = (a: number, b: number): number => a * b / gcd(a, b)

const slug = (text: string): string =>
  text.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'sound'

/** Plain durations, longest first, that together make up a number of ticks. */
const splitTicks = (ticks: number, tpq: number): Duration[] => {
  const options: { duration: Duration, ticks: number }[] = []
  for (let type = DurationType.LONGA; type >= DurationType._128TH; type -= 1) {
    for (const dots of [2, 1, 0]) {
      const duration = { type: type as DurationType, dots }
      options.push({ duration, ticks: durationTicks(duration, tpq) })
    }
  }
  options.sort((a, b) => b.ticks - a.ticks)
  const result: Duration[] = []
  let rest = ticks
  for (const option of options) {
    while (rest >= option.ticks - 1e-6 && Number.isInteger(option.ticks)) {
      result.push(option.duration)
      rest -= option.ticks
    }
  }
  return result
}

/** One chord or rest in a voice, placed inside its measure. */
interface Slot {
  tick: number
  ticks: number
  events: (Chord | Rest)[]
  /** Ratio from <time-modification>: all enclosing tuplets multiplied together. */
  ratio?: { actual: number, normal: number }
  /** Tuplets the file says begin or end on this slot, outermost first. */
  tupletStarts: { number: string, actual?: number, normal?: number }[]
  tupletStops: string[]
}

interface PartInfo {
  id: string
  name: string
  abbreviation: string
  instruments: Map<string, { name: string, midiNote?: number }>
}

export const importMusicXml = (source: string): ImportResult => {
  const score = child(parseXml(source, SKIPPED_ELEMENTS), 'score-partwise')
  if (!score) throw new Error('not a score-partwise MusicXML file')

  const skipped: Record<string, number> = {}
  const skip = (what: string) => { skipped[what] = (skipped[what] ?? 0) + 1 }

  /* ---------- part list ---------- */

  const partInfos = new Map<string, PartInfo>()
  for (const scorePart of childrenOf(child(score, 'part-list'), 'score-part')) {
    const instruments: PartInfo['instruments'] = new Map()
    for (const instrument of childrenOf(scorePart, 'score-instrument')) {
      const name = textOf(instrument, 'instrument-name') ?? ''
      instruments.set(instrument.attrs.id ?? '', { name })
    }

    for (const midi of childrenOf(scorePart, 'midi-instrument')) {
      const entry = instruments.get(midi.attrs.id ?? '')
      const unpitched = numberOf(midi, 'midi-unpitched')
      // MusicXML counts MIDI notes from 1.
      if (entry && unpitched !== undefined) entry.midiNote = unpitched - 1
    }
    const id = scorePart.attrs.id ?? ''
    partInfos.set(id, {
      id,
      name: textOf(scorePart, 'part-name') ?? id,
      abbreviation: textOf(scorePart, 'part-abbreviation') ?? '',
      instruments,
    })
  }
  const parts = childrenOf(score, 'part')
  if (parts.length === 0) throw new Error('the score has no parts')

  /* ---------- resolution ---------- */

  // Fine enough that every tuplet in the score lands on whole ticks.
  let tpq = 960
  for (const part of parts) {
    for (const measure of part.children) {
      for (const node of measure.children) {
        if (node.name !== 'note') continue
        const actual = numberOf(child(node, 'time-modification'), 'actual-notes')
        if (actual && actual > 0) tpq = lcm(tpq, actual)
      }
    }
  }

  /* ---------- score-wide measures, from the first part ---------- */

  const firstPart = parts[0]!
  const measureNodes = childrenOf(firstPart, 'measure')
  const measures: Measure[] = []
  const measureLengths: number[] = []
  const measureStarts: number[] = []
  const voltas: Volta[] = []
  const tempoKeys = new Set<string>()
  let counter = 0
  const newId = (prefix: string) => `${prefix}-${(counter += 1)}`

  {
    let divisions = 1
    let nominal = 4 * tpq
    let transposition: Transposition | undefined
    let start = 0
    let openVolta: { start: number, passes: number[] } | undefined
    measureNodes.forEach((node, m) => {
      const measure: Measure = {
        id: `m${m + 1}`, kind: 'measure', tempos: [], markers: [], jumps: [],
      }
      let cursor = 0
      let longest = 0
      for (const item of node.children) {
        if (item.name === 'attributes') {
          divisions = numberOf(item, 'divisions') ?? divisions
          const transpose = child(item, 'transpose')
          if (transpose && !transposition) {
            transposition = {
              diatonic: numberOf(transpose, 'diatonic') ?? 0,
              chromatic: numberOf(transpose, 'chromatic') ?? 0,
              octave: numberOf(transpose, 'octave-change') ?? 0,
            }
          }
          const time = child(item, 'time')
          const beats = numberOf(time, 'beats')
          const unit = numberOf(time, 'beat-type')
          if (beats && unit) {
            measure.timeSig = { id: newId('ts'), kind: 'time-signature', beats, unit }
            nominal = beats * tpq * 4 / unit
          }
          const fifths = numberOf(child(item, 'key'), 'fifths')
          if (fifths !== undefined) {
            // The part's key is written for its instrument; the score's key is concert.
            const concert = transposition ? transposeKey(fifths, transposition, 1) : fifths
            measure.key = { id: newId('key'), kind: 'key-signature', accidentals: concert }
          }
        } else if (item.name === 'note') {
          if (child(item, 'grace') || child(item, 'chord')) continue
          cursor += (numberOf(item, 'duration') ?? 0) * tpq / divisions
        } else if (item.name === 'backup')
          cursor -= (numberOf(item, 'duration') ?? 0) * tpq / divisions

        else if (item.name === 'forward')
          cursor += (numberOf(item, 'duration') ?? 0) * tpq / divisions

        else if (item.name === 'barline') {
          const style = BARLINES[textOf(item, 'bar-style') ?? '']
          if (style && item.attrs.location !== 'left') measure.barline = style
          const repeat = child(item, 'repeat')
          if (repeat?.attrs.direction === 'forward') measure.repeatStart = true
          if (repeat?.attrs.direction === 'backward')
            measure.repeatEnd = { times: Number(repeat.attrs.times ?? 2) }

          const ending = child(item, 'ending')
          if (ending?.attrs.type === 'start') {
            const passes = (ending.attrs.number ?? '1').split(/[\s,]+/).map(Number)
              .filter(n => n > 0)
            openVolta = { start: m, passes: passes.length > 0 ? passes : [1] }
          } else if (ending && openVolta) {
            voltas.push({
              id: newId('volta'),
              kind: 'volta',
              startMeasureId: `m${openVolta.start + 1}`,
              endMeasureId: measure.id,
              passes: openVolta.passes,
              ...(ending.attrs.type === 'discontinue' ? { openEnd: true } : {}),
            })
            openVolta = undefined
          }
        }
        longest = Math.max(longest, cursor)
      }
      const length = Math.round(longest)
      if (length > 0 && length !== nominal) measure.actualTicks = length
      measures.push(measure)
      measureLengths.push(measure.actualTicks ?? nominal)
      measureStarts.push(start)
      start += measure.actualTicks ?? nominal
    })
  }

  /* ---------- parts ---------- */

  const kitNotes: ImportResult['kitNotes'] = {}
  /** rit. / accel. found in any part, in written ticks from the start of the score. */
  interface TempoWords { type: 'rit' | 'accel', start: number, end: number }
  const tempoChanges: TempoWords[] = []

  const importPart = (part: XmlNode): Track => {
    const partId = part.attrs.id ?? newId('part')
    const info = partInfos.get(partId)
    const id = (kind: string) => newId(`${partId}-${kind}`)

    // Voices keep one index per staff for the whole part.
    const voicesByStaff = new Map<number, string[]>()
    let staffCount = 1
    for (const measure of part.children) {
      for (const node of measure.children) {
        if (node.name === 'attributes')
          staffCount = Math.max(staffCount, numberOf(node, 'staves') ?? 1)

        if (node.name !== 'note') continue
        const staff = numberOf(node, 'staff') ?? 1
        staffCount = Math.max(staffCount, staff)
        const voice = textOf(node, 'voice') ?? '1'
        const voices = voicesByStaff.get(staff) ?? []
        if (!voices.includes(voice)) voicesByStaff.set(staff, [...voices, voice])
      }
    }
    for (const voices of voicesByStaff.values()) voices.sort((a, b) => Number(a) - Number(b))

    const staves: Staff[] = Array.from({ length: staffCount }, (_, s) => ({
      id: `${partId}-s${s + 1}`, kind: 'staff' as const, measures: [], staffSpanners: [],
    }))
    const trackSpanners: TrackSpanner[] = []
    const attachments: Attachment[] = []
    const kit = new Map<string, KitEntry>()
    const graceToMain = new Map<string, string>()

    let divisions = 1
    let transposition: Transposition | undefined
    const pendingTies = new Map<string, { note: Note | UnpitchedNote, end: number }>()
    const pendingSlurs = new Map<string, string>()
    const pendingGlissandi = new Map<string, string>()
    /** The chord a two-note tremolo starts on, waiting for the one it ends on. */
    let pendingTremolo: string | undefined
    /** The part's key as written for the instrument, for the note a trill goes to. */
    let writtenFifths = 0
    interface OpenWedge { type: 'cresc' | 'dim', measure: number, tick: number, niente: boolean }
    const pendingWedges = new Map<string, OpenWedge>()
    let pendingPedal: { measure: number, tick: number } | undefined
    const pendingGraces = new Map<string, { slash: boolean, chords: Chord[] }>()
    const openDashes = new Map<string, TempoWords>()

    const toDuration = (node: XmlNode, ticks: number): Duration | undefined => {
      const type = NOTE_TYPES[textOf(node, 'type') ?? '']
      if (type !== undefined) return { type, dots: childrenOf(node, 'dot').length }
      return splitTicks(ticks, tpq)[0]
    }

    const kitEntry = (instrumentId: string, node: XmlNode): KitEntry => {
      const existing = kit.get(instrumentId)
      if (existing) return existing
      const instrument = info?.instruments.get(instrumentId)
      const name = instrument?.name || instrumentId
      const unpitched = child(node, 'unpitched')
      const display: Pitch = {
        step: (textOf(unpitched, 'display-step') ?? 'B').toLowerCase() as Step,
        alter: 0,
        octave: numberOf(unpitched, 'display-octave') ?? 4,
      }
      const used = new Set([...kit.values()].map(entry => entry.sound))
      let sound = slug(name)
      if (used.has(sound)) sound = `${sound}-${slug(instrumentId)}`
      const entry: KitEntry = {
        id: `${partId}-kit-${slug(instrumentId)}`,
        kind: 'kit-entry',
        name,
        sound,
        // Counted from the middle line of the staff (B4 in treble clef positions).
        displayPitch: diatonicIndex(display) - diatonicIndex({ step: Step.B, alter: 0, octave: 4 }),
        notehead: 'normal',
      }
      kit.set(instrumentId, entry)
      if (instrument?.midiNote !== undefined)
        (kitNotes[partId] ??= {})[sound] = instrument.midiNote

      return entry
    }

    childrenOf(part, 'measure').forEach((measureNode, m) => {
      if (m >= measures.length) {
        skip('measures beyond the first part')
        return
      }
      const measureId = measures[m]!.id
      const length = measureLengths[m]!
      const absStart = measureStarts[m]!
      const slots = new Map<string, Slot[]>()
      const clefs: { tick: number, clef: Clef }[][] = staves.map(() => [])
      let cursor = 0
      let lastStart = 0
      let lastChord: Chord | undefined
      /** Alterations written in this bar so far, by written step and octave. */
      const barAlters = new Map<string, number>()

      for (const node of measureNode.children) {
        switch (node.name) {
          case 'attributes': {
            divisions = numberOf(node, 'divisions') ?? divisions
            writtenFifths = numberOf(child(node, 'key'), 'fifths') ?? writtenFifths
            const transpose = child(node, 'transpose')
            if (transpose) {
              const next = {
                diatonic: numberOf(transpose, 'diatonic') ?? 0,
                chromatic: numberOf(transpose, 'chromatic') ?? 0,
                octave: numberOf(transpose, 'octave-change') ?? 0,
              }
              if (!transposition) transposition = next
              else if (JSON.stringify(next) !== JSON.stringify(transposition)) {
                skip('transposition change inside a part (instrument change)')
                transposition = next
              }
            }
            for (const clefNode of childrenOf(node, 'clef')) {
              const staff = Number(clefNode.attrs.number ?? 1) - 1
              const type = CLEFS[textOf(clefNode, 'sign') ?? '']
              if (type === undefined || !clefs[staff]) {
                skip('clef of an unsupported kind')
                continue
              }
              const tick = Math.round(cursor)
              const list = clefs[staff]
              // One clef per position: a later one at the same tick replaces the earlier.
              if (list.at(-1)?.tick === tick) list.pop()
              list.push({
                tick,
                clef: {
                  id: id('clef'),
                  kind: 'clef',
                  type,
                  line: numberOf(clefNode, 'line') ?? (type === ClefType.F ? 4 : 2),
                  octaveChange: numberOf(clefNode, 'clef-octave-change') ?? 0,
                },
              })
            }
            break
          }
          case 'backup':
            cursor -= (numberOf(node, 'duration') ?? 0) * tpq / divisions
            break
          case 'forward':
            cursor += (numberOf(node, 'duration') ?? 0) * tpq / divisions
            break
          case 'direction':
            importDirection(node, m, cursor)
            break
          case 'note': {
            const staff = numberOf(node, 'staff') ?? 1
            const voice = textOf(node, 'voice') ?? '1'
            const voiceKey = `${staff}:${voice}`
            const graceNode = child(node, 'grace')
            const isChordNote = child(node, 'chord') !== undefined
            const ticks = graceNode ? 0 : (numberOf(node, 'duration') ?? 0) * tpq / divisions
            const start = isChordNote ? lastStart : cursor
            if (!isChordNote) {
              lastStart = cursor
              cursor += ticks
            }
            const notations = childrenOf(node, 'notations')
            const notation = (name: string) => notations.flatMap(n => childrenOf(n, name))
            const timeModification = child(node, 'time-modification')
            const ratio = timeModification
              ? {
                actual: numberOf(timeModification, 'actual-notes') ?? 1,
                normal: numberOf(timeModification, 'normal-notes') ?? 1,
              }
              : undefined
            const tupletStarts: Slot['tupletStarts'] = []
            const tupletStops: string[] = []
            for (const mark of notation('tuplet')) {
              const number = mark.attrs.number ?? '1'
              if (mark.attrs.type === 'stop') tupletStops.push(number)
              if (mark.attrs.type !== 'start') continue
              const actual = numberOf(child(mark, 'tuplet-actual'), 'tuplet-number')
              const normal = numberOf(child(mark, 'tuplet-normal'), 'tuplet-number')
              tupletStarts.push({
                number,
                ...(actual !== undefined ? { actual } : {}),
                ...(normal !== undefined ? { normal } : {}),
              })
            }

            const restNode = child(node, 'rest')
            if (restNode) {
              lastChord = undefined
              // A rest is trusted for how long it lasts, not for the note value drawn:
              // whole-measure rests and some exports give a value that does not match.
              const drawn = toDuration(node, ticks)
              const matches = textOf(node, 'type') !== undefined && drawn !== undefined
                && (ratio !== undefined || Math.abs(durationTicks(drawn, tpq) - ticks) < 1)
              const whole = restNode.attrs.measure === 'yes' || !matches
              const durations = whole ? splitTicks(Math.round(ticks), tpq) : [drawn!]
              const events = durations
                .map((duration): Rest => ({ id: id('r'), kind: 'rest', duration }))
              if (events.length === 0) break
              const slot: Slot = { tick: start, ticks, events, tupletStarts, tupletStops }
              if (ratio && !whole) slot.ratio = ratio
              slots.set(voiceKey, [...(slots.get(voiceKey) ?? []), slot])
              break
            }

            // The note itself.
            let note: Note | UnpitchedNote
            let tieKey: string
            let written: Pitch | undefined
            const pitchNode = child(node, 'pitch')
            if (pitchNode) {
              written = {
                step: (textOf(pitchNode, 'step') ?? 'C').toLowerCase() as Step,
                alter: numberOf(pitchNode, 'alter') ?? 0,
                octave: numberOf(pitchNode, 'octave') ?? 4,
              }
              barAlters.set(`${written.step}${written.octave}`, written.alter)
              const pitch = transposition ? transposePitch(written, transposition, 1) : written
              note = { id: id('n'), kind: 'note', pitch }
              tieKey = `${written.step}${written.alter}/${written.octave}`
            } else {
              const instrumentId = child(node, 'instrument')?.attrs.id
                ?? [...(info?.instruments.keys() ?? [])][0] ?? 'unpitched'
              const kitEntryId = kitEntry(instrumentId, node).id
              note = { id: id('u'), kind: 'unpitched-note', kitEntryId }
              tieKey = `kit:${instrumentId}`
            }

            // Ties: the file marks both ends; link them when the second one follows directly.
            const tieTypes = [...childrenOf(node, 'tie'), ...notation('tied')]
              .map(t => t.attrs.type)
            const absTick = absStart + start
            if (tieTypes.includes('stop')) {
              const pending = pendingTies.get(tieKey)
              if (pending && Math.abs(pending.end - absTick) < 1 && !graceNode) {
                const targets = Array.isArray(pending.note.tie) ? pending.note.tie : []
                pending.note.tie = [...targets, { to: note.id }]
              } else if (pending) skip('tie whose second note does not follow directly')
              pendingTies.delete(tieKey)
            }
            if (tieTypes.includes('let-ring')) note.tie = { letRing: true }
            else if (tieTypes.includes('start') && !graceNode)
              pendingTies.set(tieKey, { note, end: absTick + ticks })


            // The chord it belongs to.
            let chord: Chord
            if (isChordNote && lastChord) {
              chord = lastChord
              chord.notes.push(note)
            } else {
              const duration = toDuration(node, ticks)
                ?? { type: DurationType._8TH, dots: 0 }
              chord = { id: id('c'), kind: 'chord', duration, notes: [note], articulations: [] }
              lastChord = chord
              if (graceNode) {
                const group = pendingGraces.get(voiceKey)
                  ?? { slash: graceNode.attrs.slash === 'yes', chords: [] }
                group.chords.push(chord)
                pendingGraces.set(voiceKey, group)
              } else {
                const graces = pendingGraces.get(voiceKey)
                if (graces) {
                  chord.graceNotes = {
                    id: id('g'), kind: 'grace-chords', slash: graces.slash, chords: graces.chords,
                  }
                  for (const grace of graces.chords) graceToMain.set(grace.id, chord.id)
                  pendingGraces.delete(voiceKey)
                }
                const slot: Slot = {
                  tick: start, ticks, events: [chord], tupletStarts, tupletStops,
                }
                if (ratio) slot.ratio = ratio
                slots.set(voiceKey, [...(slots.get(voiceKey) ?? []), slot])
              }
            }
            if (isChordNote && !graceNode && lastChord) {
              // Tuplet marks can sit on any note of a chord.
              const last = slots.get(voiceKey)?.at(-1)
              last?.tupletStarts.push(...tupletStarts)
              last?.tupletStops.push(...tupletStops)
            }

            // Articulations.
            for (const mark of notation('articulations').flatMap(n => n.children)) {
              let types = ARTICULATIONS[mark.name]
              if (mark.name === 'strong-accent') {
                skip('marcato (strong accent) stored as a plain accent')
                types = [ArticulationType.ACCENT]
              }
              if (!types) {
                skip(`articulation <${mark.name}>`)
                continue
              }
              for (const type of types) {
                if (chord.articulations.some(a => a.type === type)) continue
                chord.articulations.push({ id: id('a'), kind: 'articulation', type })
              }
            }

            // Ornaments and other marks. The model has no notation for them, so the
            // ones playback can use are kept as one-note technique markings.
            const mark = (techniqueId: string) => {
              const already = attachments.some(a => a.kind === 'technique'
                && a.techniqueId === techniqueId && a.at.type === 'element' && a.at.id === chord.id)
              if (already || graceNode) return
              attachments.push({
                id: id('tech'),
                kind: 'technique',
                techniqueId,
                scope: 'this',
                at: { type: 'element', id: chord.id },
              })
            }
            const ornaments = notation('ornaments').flatMap(n => n.children)
            for (const ornament of ornaments) {
              if (ornament.name === 'tremolo') {
                // One or two strokes are a measured subdivision; three mean "as fast as
                // possible". A two-note tremolo is always unmeasured.
                const type = ornament.attrs.type ?? 'single'
                const strokes = Number(ornament.text) || 3
                if (type === 'start') pendingTremolo = chord.id
                else if (type === 'stop') {
                  // Between two notes: a fingered tremolo, played as a trill on both.
                  if (pendingTremolo !== undefined && pendingTremolo !== chord.id) {
                    trackSpanners.push({
                      id: id('trem'),
                      kind: 'custom-spanner',
                      techniqueId: 'core:tremolo-pair',
                      start: { type: 'element', id: pendingTremolo },
                      end: { type: 'element', id: chord.id },
                      includeEnd: true,
                    })
                  }
                  pendingTremolo = undefined
                } else mark(strokes < 3 ? `core:tremolo-${strokes}` : 'core:tremolo')
              } else if (ornament.name === 'trill-mark') {
                // The library trills to a note a half or a whole step up: which one is read
                // like a player does, from an accidental on the trill, the bar, or the key.
                const interval = written && trillInterval(written, ornaments, barAlters,
                  writtenFifths)
                if (interval === 1) mark('core:trill-half')
                else if (interval === 2) mark('core:trill-whole')
                else {
                  mark('core:trill')
                  skip('trill to a note that is not a half or whole step up')
                }
              } else if (ornament.name !== 'wavy-line' && ornament.name !== 'accidental-mark')
                skip(`ornament <${ornament.name}>`)
            }
            if (notation('arpeggiate').length > 0) mark('core:arpeggio')
            if (notation('fermata').length > 0) skip('<fermata>')

            // Glissandi and slides, from one chord to another.
            for (const line of [...notation('glissando'), ...notation('slide')]) {
              const key = `${line.name}:${line.attrs.number ?? '1'}`
              if (line.attrs.type === 'start') pendingGlissandi.set(key, chord.id)
              if (line.attrs.type !== 'stop') continue
              const from = pendingGlissandi.get(key)
              pendingGlissandi.delete(key)
              if (from === undefined || from === chord.id) continue
              trackSpanners.push({
                id: id('gliss'),
                kind: 'custom-spanner',
                techniqueId: 'core:gliss',
                start: { type: 'element', id: from },
                end: { type: 'element', id: chord.id },
              })
            }


            // Slurs, between chords.
            for (const slur of notation('slur')) {
              const number = slur.attrs.number ?? '1'
              if (slur.attrs.type === 'start') pendingSlurs.set(number, chord.id)
              if (slur.attrs.type !== 'stop') continue
              const from = pendingSlurs.get(number)
              pendingSlurs.delete(number)
              if (from === undefined) skip('slur that ends without a start')
              else {
                trackSpanners.push({
                  id: id('slur'),
                  kind: 'slur',
                  start: { type: 'element', id: from },
                  end: { type: 'element', id: chord.id },
                })
              }
            }
            break
          }
          default:
        }
      }

      // Build the voices of each staff for this measure.
      staves.forEach((staff, s) => {
        const voices = (voicesByStaff.get(s + 1) ?? []).flatMap((voice, index) => {
          const list = slots.get(`${s + 1}:${voice}`)
          return list ? [{ index, events: buildVoice(list, length) }] : []
        })
        if (voices.length === 0) {
          const durations = splitTicks(length, tpq)
          voices.push({
            index: 0,
            events: durations.map((duration): Rest => ({
              id: id('r'),
              kind: 'rest',
              duration,
              ...(durations.length === 1 ? { fullMeasure: true } : {}),
            })),
          })
        }
        const staffClefs = clefs[s]!
        if (m === 0 && staffClefs[0]?.tick !== 0) {
          staffClefs.unshift({
            tick: 0,
            clef: { id: id('clef'), kind: 'clef', type: ClefType.G, line: 2, octaveChange: 0 },
          })
        }
        const staffMeasure: StaffMeasure = {
          id: id('sm'),
          kind: 'staff-measure',
          measureId,
          clefs: staffClefs.filter(c => c.tick < length || length === 0),
          voices: voices.map(voice => ({ id: id('v'), kind: 'voice', ...voice })),
        }
        staff.measures.push(staffMeasure)
      })
    })

    /** Spaces that fill a stretch of a voice nothing is written in. */
    function spaces(ticks: number): Space[] {
      if (ticks < 1) return []
      const durations = splitTicks(Math.round(ticks), tpq)
      if (durations.length === 0) skip('gap in a voice too short to fill')
      return durations.map(duration => ({ id: id('s'), kind: 'space', duration }))
    }

    /** Written length of an event: a tuplet counts as the time it takes up. */
    function writtenTicks(event: VoiceEvent): number {
      return event.kind === 'tuplet'
        ? durationTicks(event.unit, tpq) * event.normal
        : durationTicks(event.duration, tpq)
    }

    /** Slots of one voice → its events: gaps filled, tuplets grouped (also nested ones). */
    function buildVoice(list: Slot[], length: number): VoiceEvent[] {
      const events: VoiceEvent[] = []
      interface Open {
        /** Undefined when the file gave only the ratio, without marking start and end. */
        number: string | undefined
        actual: number
        normal: number
        events: VoiceEvent[]
        startTick: number
      }
      const open: Open[] = []
      let position = 0

      const close = () => {
        const tuplet = open.pop()
        if (!tuplet) return
        const target = open.at(-1)?.events ?? events
        const written = tuplet.events.reduce((sum, event) => sum + writtenTicks(event), 0)
        const unit = splitTicks(written / tuplet.actual, tpq)
        if (unit.length === 1 && Number.isInteger(written / tuplet.actual)) {
          target.push({
            id: id('t'),
            kind: 'tuplet',
            actual: tuplet.actual,
            normal: tuplet.normal,
            unit: unit[0]!,
            events: tuplet.events,
          })
        } else if (open.length === 0) {
          // Not a whole tuplet: keep its time as a gap so the measure still adds up.
          skip('tuplet that does not add up; replaced by a gap')
          target.push(...spaces(position - tuplet.startTick))
        } else
          target.push(...tuplet.events)

      }

      for (const slot of list) {
        if (slot.tick < position - 1) {
          skip('note overlapping the previous one in its voice')
          continue
        }
        // A tuplet known only by its ratio ends where the ratio changes.
        const implicit = open.at(-1)?.number === undefined ? open.at(-1) : undefined
        const sameRatio = implicit && slot.ratio && slot.tupletStarts.length === 0
          && slot.ratio.actual === implicit.actual && slot.ratio.normal === implicit.normal
        if (implicit && !sameRatio) close()

        if (open.length === 0) events.push(...spaces(slot.tick - position))

        for (const start of slot.tupletStarts) {
          // The inner ratio is what is left of the full ratio after the outer tuplets.
          const outerActual = open.reduce((product, tuplet) => product * tuplet.actual, 1)
          const outerNormal = open.reduce((product, tuplet) => product * tuplet.normal, 1)
          const actual = start.actual ?? (slot.ratio ? slot.ratio.actual / outerActual : 1)
          const normal = start.normal ?? (slot.ratio ? slot.ratio.normal / outerNormal : 1)
          if (!Number.isInteger(actual) || !Number.isInteger(normal) || actual < 1 || normal < 1) {
            skip('tuplet with a ratio that cannot be read')
            continue
          }
          open.push({ number: start.number, actual, normal, events: [], startTick: slot.tick })
        }
        if (slot.ratio && open.length === 0)
          open.push({ number: undefined, ...slot.ratio, events: [], startTick: slot.tick })


        const single = slot.events.length === 1 ? slot.events[0] : undefined
        const fills = list.length === 1 && Math.abs(slot.ticks - length) < 1
        if (single?.kind === 'rest' && fills && open.length === 0) single.fullMeasure = true
        ;(open.at(-1)?.events ?? events).push(...slot.events)
        position = slot.tick + slot.ticks

        for (const number of slot.tupletStops) {
          const depth = open.findLastIndex(tuplet => tuplet.number === number)
          while (depth >= 0 && open.length > depth) close()
        }
      }
      while (open.length > 0) close()
      events.push(...spaces(length - position))
      return events
    }

    function importDirection(node: XmlNode, m: number, cursor: number) {
      const offset = (numberOf(node, 'offset') ?? 0) * tpq / divisions
      const measureId = measures[m]!.id
      const length = measureLengths[m]!
      const tick = Math.min(length, Math.max(0, Math.round(cursor + offset)))
      const anchor = { type: 'tick' as const, measureId, tick }
      let tempoSet = false
      let tempoWords: TempoWords | undefined

      for (const type of childrenOf(node, 'direction-type').flatMap(t => t.children)) {
        switch (type.name) {
          case 'dynamics':
            for (const mark of type.children) {
              const dynamic = DYNAMICS[mark.name] ?? DYNAMIC_SUBSTITUTES[mark.name]
              if (!dynamic) {
                skip(`dynamic <${mark.name}>`)
                continue
              }
              if (!DYNAMICS[mark.name]) skip(`dynamic "${mark.name}" stored as "${dynamic}"`)
              attachments.push({ id: id('dyn'), kind: 'dynamic', type: dynamic, at: anchor })
            }
            break
          case 'wedge': {
            const number = type.attrs.number ?? '1'
            if (type.attrs.type === 'crescendo' || type.attrs.type === 'diminuendo') {
              pendingWedges.set(number, {
                type: type.attrs.type === 'crescendo' ? 'cresc' : 'dim',
                measure: m,
                tick,
                niente: type.attrs.niente === 'yes',
              })
            } else if (type.attrs.type === 'stop') {
              const open = pendingWedges.get(number)
              pendingWedges.delete(number)
              if (!open) break
              if (measureStarts[open.measure]! + open.tick >= measureStarts[m]! + tick) {
                skip('hairpin with no length')
                break
              }
              trackSpanners.push({
                id: id('hairpin'),
                kind: 'hairpin',
                type: open.type,
                start: { type: 'tick', measureId: measures[open.measure]!.id, tick: open.tick },
                end: anchor,
                ...(open.niente ? { niente: true } : {}),
              })
            }
            break
          }
          case 'pedal': {
            const kind = type.attrs.type
            if ((kind === 'stop' || kind === 'change') && pendingPedal) {
              const from = measureStarts[pendingPedal.measure]! + pendingPedal.tick
              if (from < measureStarts[m]! + tick) {
                trackSpanners.push({
                  id: id('pedal'),
                  kind: 'pedal',
                  type: 'sustain',
                  start: {
                    type: 'tick',
                    measureId: measures[pendingPedal.measure]!.id,
                    tick: pendingPedal.tick,
                  },
                  end: anchor,
                })
              }
              pendingPedal = undefined
            }
            if (kind === 'start' || kind === 'change') pendingPedal = { measure: m, tick }
            break
          }
          case 'words': {
            const text = type.text.trim()
            const technique = TECHNIQUE_WORDS.find(([pattern]) => pattern.test(text))
            if (technique) {
              attachments.push({
                id: id('tech'),
                kind: 'technique',
                techniqueId: technique[1],
                text,
                scope: 'until-next',
                at: anchor,
              })
            } else if (/^(rit|rall|accel|string)/i.test(text)) {
              // Ends at its dashed line if it has one, otherwise at the barline.
              tempoWords = {
                type: /^(accel|string)/i.test(text) ? 'accel' : 'rit',
                start: measureStarts[m]! + tick,
                end: measureStarts[m]! + length,
              }
              tempoChanges.push(tempoWords)
            } else if (text) {
              // Anything else is kept for the page; playback does not read it.
              attachments.push({ id: id('text'), kind: 'text', text, at: anchor })
            }
            break
          }
          case 'dashes': {
            const number = type.attrs.number ?? '1'
            if (type.attrs.type === 'start' && tempoWords) openDashes.set(number, tempoWords)
            else if (type.attrs.type === 'stop' && openDashes.has(number)) {
              openDashes.get(number)!.end = measureStarts[m]! + tick
              openDashes.delete(number)
            } else if (type.attrs.type === 'start')
              skip('dashed line after a text that is not a tempo change')

            break
          }
          case 'metronome': {
            const unitType = NOTE_TYPES[textOf(type, 'beat-unit') ?? '']
            const bpm = numberOf(type, 'per-minute')
            if (unitType === undefined || !bpm) {
              skip('metronome mark that is not a plain number')
              break
            }
            addTempo(m, tick, {
              type: unitType, dots: childrenOf(type, 'beat-unit-dot').length,
            }, bpm)
            tempoSet = true
            break
          }
          default:
            skip(`direction <${type.name}>`)
        }
      }
      const soundTempo = Number(child(node, 'sound')?.attrs.tempo)
      if (!tempoSet && soundTempo > 0)
        addTempo(m, tick, { type: DurationType.QUARTER, dots: 0 }, soundTempo)

    }

    function addTempo(m: number, tick: number, unit: Duration, bpm: number) {
      const inside = Math.min(tick, measureLengths[m]! - 1)
      if (tempoKeys.has(`${m}:${inside}`)) return
      tempoKeys.add(`${m}:${inside}`)
      const tempo: Tempo = { id: newId('tempo'), kind: 'tempo', unit, bpm, tick: inside }
      measures[m]!.tempos.push(tempo)
    }

    // A slur that starts or ends on a grace note is moved to the note it decorates.
    const spanners = trackSpanners.flatMap((spanner): TrackSpanner[] => {
      if (spanner.kind !== 'slur') return [spanner]
      const start = graceToMain.get(spanner.start.id) ?? spanner.start.id
      const end = graceToMain.get(spanner.end.id) ?? spanner.end.id
      if (start === end) {
        skip('slur from a grace note to its own main note')
        return []
      }
      return [{
        ...spanner, start: { type: 'element', id: start }, end: { type: 'element', id: end },
      }]
    })

    for (const staff of staves) {
      while (staff.measures.length < measures.length) {
        const m = staff.measures.length
        const durations = splitTicks(measureLengths[m]!, tpq)
        staff.measures.push({
          id: id('sm'),
          kind: 'staff-measure',
          measureId: measures[m]!.id,
          clefs: [],
          voices: [{
            id: id('v'),
            kind: 'voice',
            index: 0,
            events: durations.map((duration): Rest => ({ id: id('r'), kind: 'rest', duration })),
          }],
        })
      }
    }

    const hasTransposition = transposition
      && Object.values(transposition).some(value => value !== 0)
    return {
      id: partId,
      kind: 'track',
      instrumentName: info?.name ?? partId,
      instrumentAbbr: info?.abbreviation ?? '',
      staves,
      trackSpanners: spanners,
      attachments,
      ...(hasTransposition && transposition ? { transposition } : {}),
      ...(kit.size > 0 ? { kit: [...kit.values()] } : {}),
    }
  }

  const tracks = parts.map(importPart)
  for (const measure of measures) measure.tempos.sort((a, b) => a.tick - b.tick)

  // Gradual tempo changes. One that leads straight into a tempo mark in its own
  // direction (a rit. into a slower tempo) aims at that tempo; otherwise the target is
  // left open for playback to choose.
  const anchorAt = (tick: number) => {
    const m = Math.max(0, measureStarts.findLastIndex(start => start <= tick))
    return { type: 'tick' as const, measureId: measures[m]!.id, tick: tick - measureStarts[m]! }
  }
  const marks = measures.flatMap((measure, m) => measure.tempos.map(tempo => ({
    tick: measureStarts[m]! + tempo.tick,
    qpm: tempo.bpm * durationTicks(tempo.unit, tpq) / tpq,
  })))
  const globalSpanners: MusicData['globalSpanners'] = [...voltas]
  const seenChanges = new Set<number>()
  for (const change of tempoChanges.sort((a, b) => a.start - b.start)) {
    if (seenChanges.has(change.start) || change.end <= change.start) continue
    seenChanges.add(change.start)
    const before = marks.findLast(mark => mark.tick <= change.start)?.qpm
    const next = marks.find(mark => mark.tick >= change.end && mark.tick <= change.end + tpq)
    const leadsThere = before !== undefined && next !== undefined
      && (change.type === 'rit' ? next.qpm < before : next.qpm > before)
    globalSpanners.push({
      id: newId('tempo-change'),
      kind: 'tempo-change',
      type: change.type,
      start: anchorAt(change.start),
      end: anchorAt(change.end),
      ...(leadsThere ? { targetTempo: next.qpm } : {}),
    })
  }

  // The model needs a time signature and a key on the first measure.
  const first = measures[0]
  if (first && !first.timeSig)
    first.timeSig = { id: newId('ts'), kind: 'time-signature', beats: 4, unit: 4 }

  if (first && !first.key) first.key = { id: newId('key'), kind: 'key-signature', accidentals: 0 }

  const composer = childrenOf(child(score, 'identification'), 'creator')
    .find(creator => creator.attrs.type === 'composer')?.text
  return {
    music: {
      title: textOf(child(score, 'work'), 'work-title') ?? textOf(score, 'movement-title') ?? '',
      composer: composer ?? '',
      tpq,
      measures,
      tracks,
      globalSpanners,
    },
    kitNotes,
    skipped,
  }
}
