/**
 * Small helpers for writing scores by hand (examples and tests).
 * Not part of the data model: anything built here is plain MusicData.
 */
import type {
  Articulation,
  ArticulationType,
  Chord,
  Clef,
  Duration,
  GraceChords,
  KeySignature,
  Measure,
  Note,
  Pitch,
  Rest,
  Space,
  StaffMeasure,
  Tempo,
  TimeSignature,
  Tuplet,
  UnpitchedNote,
  Voice,
  VoiceEvent,
} from '@/core/types/MusicData'
import { ClefType, Step } from '@/core/types/MusicData'

type BeamMode = NonNullable<Chord['beam']>

interface NoteOptions {
  id?: string
  /** One note id, or several when the tie leads into more than one ending. */
  tieTo?: string | string[]
}

interface ChordOptions {
  id?: string
  articulations?: ArticulationType[]
  beam?: BeamMode
  grace?: GraceChords
}

const ALTERS: Record<string, number> = { 'bb': -2, 'b': -1, '': 0, '#': 1, '##': 2, 'x': 2 }

export class ScoreBuilder {
  private count = 0

  constructor(private readonly prefix: string) {}

  id(kind: string): string {
    this.count += 1
    return `${this.prefix}-${kind}${this.count}`
  }

  /** "4" quarter, "8." dotted eighth, "2.." double-dotted half, "1" whole, "0" double whole. */
  duration(text: string): Duration {
    const match = /^(\d+)(\.*)$/.exec(text)
    if (!match) throw new Error(`bad duration "${text}"`)
    const value = Number(match[1])
    const type = value === 0 ? 3 : Math.log2(4 / value)
    if (!Number.isInteger(type)) throw new Error(`bad duration "${text}"`)
    return { type, dots: match[2]!.length }
  }

  /** "C4", "F#5", "Bb3", "Ebb4", "Gx2". */
  pitch(text: string): Pitch {
    const match = /^([A-G])(bb|b|##|#|x)?(\d)$/.exec(text)
    if (!match) throw new Error(`bad pitch "${text}"`)
    return {
      step: match[1]!.toLowerCase() as Step,
      alter: ALTERS[match[2] ?? '']!,
      octave: Number(match[3]),
    }
  }

  note(pitch: string, options: NoteOptions = {}): Note {
    return {
      id: options.id ?? this.id('n'),
      kind: 'note',
      pitch: this.pitch(pitch),
      ...(options.tieTo !== undefined
        ? { tie: [options.tieTo].flat().map(to => ({ to })) }
        : {}),
    }
  }

  unpitched(kitEntryId: string, id?: string): UnpitchedNote {
    return { id: id ?? this.id('u'), kind: 'unpitched-note', kitEntryId }
  }

  /** Notes are pitch strings ("C4"), "@kitId" for unpitched notes, or prebuilt notes. */
  chord(
    duration: string,
    notes: (string | Note | UnpitchedNote)[],
    options: ChordOptions = {},
  ): Chord {
    const toNote = (n: string | Note | UnpitchedNote) => {
      if (typeof n !== 'string') return n
      return n.startsWith('@') ? this.unpitched(n.slice(1)) : this.note(n)
    }
    const toArticulation = (type: ArticulationType): Articulation =>
      ({ id: this.id('a'), kind: 'articulation', type })
    return {
      id: options.id ?? this.id('c'),
      kind: 'chord',
      duration: this.duration(duration),
      notes: notes.map(toNote),
      articulations: (options.articulations ?? []).map(toArticulation),
      ...(options.beam ? { beam: options.beam } : {}),
      ...(options.grace ? { graceNotes: options.grace } : {}),
    }
  }

  grace(
    chords: Chord[],
    options: { slash?: boolean } = {},
  ): GraceChords {
    return {
      id: this.id('g'),
      kind: 'grace-chords',
      chords: chords.map(({ graceNotes: _, ...chord }) => chord),
      ...options,
    }
  }

  rest(duration: string, options: { id?: string, fullMeasure?: boolean } = {}): Rest {
    return {
      id: options.id ?? this.id('r'),
      kind: 'rest',
      duration: this.duration(duration),
      ...(options.fullMeasure ? { fullMeasure: true } : {}),
    }
  }

  space(duration: string): Space {
    return { id: this.id('s'), kind: 'space', duration: this.duration(duration) }
  }

  tuplet(actual: number, normal: number, unit: string, events: VoiceEvent[]): Tuplet {
    return { id: this.id('t'), kind: 'tuplet', actual, normal, unit: this.duration(unit), events }
  }

  /**
   * Quick sequence of events: "4:C5 8:D5,F5 4:r 2:s 8:@snare".
   * `r` is a rest, `s` is a space, comma-separated notes make a chord.
   */
  seq(text: string): VoiceEvent[] {
    return text.trim().split(/\s+/).map(token => {
      const [duration, content] = token.split(':')
      if (!duration || !content) throw new Error(`bad token "${token}"`)
      if (content === 'r') return this.rest(duration)
      if (content === 's') return this.space(duration)
      return this.chord(duration, content.split(','))
    })
  }

  /** The index is set by `staffMeasure`, from the voice's position in the measure. */
  voice(...parts: (VoiceEvent | VoiceEvent[])[]): Voice {
    return { id: this.id('v'), kind: 'voice', index: 0, events: parts.flat() }
  }

  clef(type: ClefType, line: number, octaveChange = 0): Clef {
    return { id: this.id('clef'), kind: 'clef', type, line, octaveChange }
  }

  treble(): Clef { return this.clef(ClefType.G, 2) }
  bass(): Clef { return this.clef(ClefType.F, 4) }
  percussion(): Clef { return this.clef(ClefType.PERCUSSION, 3) }

  staffMeasure(
    measureId: string,
    voices: Voice[],
    clefs: { tick: number, clef: Clef }[] = [],
  ): StaffMeasure {
    return {
      id: this.id('sm'),
      kind: 'staff-measure',
      measureId,
      clefs,
      voices: voices.map((voice, index) => ({ ...voice, index })),
    }
  }

  timeSig(beats: number, unit: number): TimeSignature {
    return { id: this.id('ts'), kind: 'time-signature', beats, unit }
  }

  key(accidentals: number): KeySignature {
    return { id: this.id('ks'), kind: 'key-signature', accidentals }
  }

  tempo(unit: string, value: number, options: { tick?: number, text?: string } = {}): Tempo {
    return {
      id: this.id('tempo'),
      kind: 'tempo',
      unit: this.duration(unit),
      bpm: value,
      tick: options.tick ?? 0,
      ...(options.text !== undefined ? { text: options.text } : {}),
    }
  }

  measure(id: string, fields: Partial<Omit<Measure, 'id' | 'kind'>> = {}): Measure {
    return { id, kind: 'measure', tempos: [], markers: [], jumps: [], ...fields }
  }
}
