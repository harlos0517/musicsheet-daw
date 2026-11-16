import type { ChangeMode } from '@/core/types/InterpretationProfile'
import type { CurveParameter, CurveType } from '@/core/types/MusicEvent'

export interface MusicEventOverrideBase {
  id: string
  kind: string
  sourceId: string
  passes?: number[] // defaults to all passes
}

export interface NoteEventOverride extends MusicEventOverrideBase {
  kind: 'note-event-override'
  pitch?: number | { sound: string }
  durationTicks?: { mode: ChangeMode, value: number } // changes current new length
  startTickOffset?: number // does not move note end
  velocity?: { mode: ChangeMode, value: number }
  techniques?: { add?: string[], remove?: string[] }
  startOffsetMs?: number
}

export interface CurveEventOverride extends MusicEventOverrideBase {
  kind: 'curve-event-override'
  segmentIndex: number
  parameter?: CurveParameter
  appliesTo?: { staffId: string, voiceIndex?: number }[]
  durationTicks?: { mode: ChangeMode, value: number } // changes current new length
  startTickOffset?: number // does not change curve end tick
  fromValue?: { mode: ChangeMode, value: number }
  toValue?: { mode: ChangeMode, value: number }
  curveType?: CurveType
  steepness?: number
}

export type MusicEventOverride = NoteEventOverride | CurveEventOverride
