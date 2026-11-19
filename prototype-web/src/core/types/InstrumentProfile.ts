import type { ChangeMode, CurveShape } from '@/core/types/InterpretationProfile'
import type { CurveParameter, SlurPosition } from '@/core/types/MusicEvent'

export default interface InstrumentProfile {
  id: string
  name: string // "Library A – Violin"
  trackDelayMs: number // set on DAW track, not in actual MIDI output
  velocity:
    | { fixed: number } // 1 - 127
    | { mapping: OutputRange } // use event velocity value, 0 - 1
  techniqueMappings: TechniqueMapping[]
  // if a note matches several Mapping, changes accumulate/merge,
  // except for keyswitch/channels (later wins) and mode 'set' on numeric values.
  curveMappings: CurveMapping[] // dynamics, pedals, custom parameter
  defaultChannel: number // note without channels after mapping goes here.
  kit?: Record<string, number | number[]> // sound → MIDI note ('snare' → 38); several
  // notes take turns, two keys of the same drum so that repeats do not sound the same
  pitchOffset?: number // semitones added to every pitched note; for a patch that is
  // mapped away from sounding pitch (a piccolo or bass patch played at written pitch)
  keyswitchLeadMs?: number // how early to send keyswitches
  dampKeyswitch?: { // a temporary key that stops the sound: held until the next note or
    // for a beat, letting go switches the patch back
    note: number
    velocity: number
    atRests?: boolean // where a rest follows a note (its written end); default: true
    afterTechniques?: string[] // also right where such a note's sound ends (staccato)
  }
}

export interface TechniqueMapping {
  matches: NoteMatch[] // all must be present
  keyswitch?: { note: number, velocity: number }
  cc?: { number: number, value: number | ByDuration }[]
  // a controller that a curve mapping also drives is held at this value while the
  // note sounds, and goes back to the curve afterwards.
  channelMode?: 'replace' | 'add' // defaults to 'replace'
  channels?: number[] // defaults to all channel, otherwise only apply to specified channels
  durationMs?: { mode: ChangeMode, value: number }
  // duration cut off before next same note start to prevent overlapping
  startOffsetMs?: number // applied after duration
  velocity?: { mode: ChangeMode, value: number }
  notes?: number[] // played instead of the note's own pitch; several take turns,
  // e.g. two keys of the same hit so that repeats do not sound the same
  alignToEnd?: boolean // the note keeps the end it is played to (before durationMs): with
  // durationMs it starts that long before it (a sample that peaks at its end)
}

/** A value chosen by how long the note is written, in real time. */
export interface ByDuration {
  shortMs: number // at or below this length the value is `atShort`
  longMs: number // at or above this length the value is `atLong`; linear in between
  atShort: number
  atLong: number
}

export interface CurveMapping {
  parameter: CurveParameter
  defaultValue: number // 0 ~ 1
  cc?: { number: number, mapping: OutputRange }[] // to every channel it uses
  keyswitch?: KeyswitchRange[] // later matches win; to every channel it uses
  channels?: number[]
}

export interface OutputRange {
  low: number // value for 0
  high: number // value for 1
  curve?: CurveShape // how 0–1 is spread between them; default: linear. Power with
  // steepness above 0 puts soft dynamics closer together and loud ones further apart;
  // below 0, the other way round
}

export interface KeyswitchRange {
  min: number
  max: number
  note: number // the keyswitch note number
  velocity: number
}

export type NoteMatch =
  | { kind: 'technique', techniqueId: string }
  | { kind: 'curve', parameter: CurveParameter, min: number, max: number }
  | { kind: 'slur', position: SlurPosition }
