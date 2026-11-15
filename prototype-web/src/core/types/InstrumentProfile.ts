import type { ChangeMode } from '@/core/types/IntentProfile'
import type { CurveFor, SlurPosition } from '@/core/types/MusicEvent'

export default interface InstrumentProfile {
  id: string
  name: string // "Library A – Violin"
  offsetMs: number // set on DAW track, not in actual MIDI output
  velocity:
    | { fixed: number }
    | { mapping: RangeMapping } // use event velocity value
  techniqueMappings: TechniqueMapping[]
  // if a note matches several Mapping, changes accumulate/merge,
  // except for keyswitch/channels (later wins) and mode 'set' on numeric values.
  curveMappings: CurveMapping[] // dynamics, pedals, custom curveFor
  defaultChannel: number // note without channels after mapping goes here.
  fallback: Omit<TechniqueMapping, 'matches' | 'channels' | 'channelMode'>
  kit?: Record<string, number> // sound → MIDI note ('snare' → 38)
  keyswitchLeadMs?: number // how early to send keyswitches
}

export interface TechniqueMapping {
  matches: EventMatch[] // all must be present
  keyswitch?: number
  cc?: { number: number, value: number }[]
  channelMode?: 'replace' | 'add' // defaults to 'replace'
  channels?: number[] // defaults to all channel, otherwise only apply to specified channels
  durationMs?: { mode: ChangeMode, value: number }
  // duration cut off before next note start to prevent overlapping
  startOffsetMs?: number // applied after duration
  velocity?: { mode: ChangeMode, value: number }
}

export interface CurveMapping {
  for: CurveFor
  default: number // 0 ~ 1
  cc?: { number: number, mapping: RangeMapping }[] // to every channel it uses
  keyswitch?: RangeToKeyswitchMapping[] // later matches win; to every channel it uses
  channels?: number[]
}

export interface RangeMapping {
  low: number // value for 0
  high: number // value for 1
}

export interface RangeToKeyswitchMapping {
  min: number
  max: number
  number: number // the keyswitch note number
  velocity: number
}

export type EventMatch =
  | { type: 'technique', id: string }
  | { type: 'curve', curveFor: CurveFor, min: number, max: number }
  | { type: 'slur', position: SlurPosition }
