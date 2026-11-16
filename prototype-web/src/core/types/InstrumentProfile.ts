import type { ChangeMode } from '@/core/types/InterpretationProfile'
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
  kit?: Record<string, number> // sound → MIDI note ('snare' → 38)
  keyswitchLeadMs?: number // how early to send keyswitches
}

export interface TechniqueMapping {
  matches: NoteMatch[] // all must be present
  keyswitch?: { note: number, velocity: number }
  cc?: { number: number, value: number }[]
  channelMode?: 'replace' | 'add' // defaults to 'replace'
  channels?: number[] // defaults to all channel, otherwise only apply to specified channels
  durationMs?: { mode: ChangeMode, value: number }
  // duration cut off before next same note start to prevent overlapping
  startOffsetMs?: number // applied after duration
  velocity?: { mode: ChangeMode, value: number }
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
