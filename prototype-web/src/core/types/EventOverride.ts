import type { ChangeMode } from '@/core/types/IntentProfile'
import type { CurveFor, CurveType } from '@/core/types/MusicEvent'

export interface MusicEventOverrideBase {
  id: string
  kind: string
  sourceId: string
  passes?: number[] // defaults to all passes
}

export interface MusicNoteEventOverride extends MusicEventOverrideBase {
  kind: 'music-note-event-override'
  pitch?: number | { sound: string }
  durationTicks?: { mode: ChangeMode, value: number } // changes current new length
  startTickOffset?: number // does not move note end
  velocity?: { mode: ChangeMode, value: number }
  techniques?: { add?: string[], remove?: string[] }
  startOffsetMs?: number
}

export interface MusicCurveEventOverride extends MusicEventOverrideBase {
  kind: 'music-curve-event-override'
  segment: number
  curveFor?: CurveFor
  applies?: { staffId: string, voiceIndex: number }[]
  durationTicks?: { mode: ChangeMode, value: number } // changes current new length
  tickOffset?: number // does not change curve end tick
  fromValue?: { mode: ChangeMode, value: number }
  toValue?: { mode: ChangeMode, value: number }
  curveType?: CurveType
  a?: number
}

export type MusicEventOverride = MusicNoteEventOverride | MusicCurveEventOverride
