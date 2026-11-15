export interface MusicEventBase {
  kind: string
  id: string // ${sourceId}@${pass}#${segment}
  sourceIds: string[] // notes or elements it came from (could be several)
  trackId: string
}

export interface MusicNoteEvent extends MusicEventBase {
  kind: 'music-note-event'
  staffId: string
  voiceIndex: number
  pass: number
  pitch: number | { sound: string }  // sounding MIDI number, or a kit sound
  startTick: number // played-order ticks (after repeat expansion)
  notatedTicks: number // before rules; lets an instrument ignore length rules
  durationTicks: number // after rules
  velocity: number // 0 ~ 1, attack strength
  techniques: string[] // registry IDs: ['core:short', 'core:accent']
  startOffsetMs?: number // humanizing / attack compensation, applied after the tempo map
  slur?: SlurPosition
}

export interface MusicCurveEvent extends MusicEventBase {
  // overlapping curve for the same curveFor shouldn't be legal;
  // if that ever happens, the latter wins.
  // value holds between curve segments.
  kind: 'music-curve-event'
  segment: number
  curveFor: CurveFor
  // curve could be tied to a technique group with continuous values
  applies?: { staffId: string, voiceIndex: number }[]
  tick: number
  durationTicks: number
  fromValue: number // 0 ~ 1
  toValue: number // 0 ~ 1
  curveType?: CurveType // when duration is 0, this is ignored and 'hold' is used
  a?: number // how intense the curve is
}

export type CurveType = 'hold' | 'linear' | 'exp' | 'power' | 'ease'
// hold: holds start value and change instantly at end

export type MusicEvent = MusicNoteEvent | MusicCurveEvent

export type SlurPosition = 'first' | 'inner' | 'last'

export type CurveFor =
  | 'dynamics' | 'sustain' | 'sostenuto' | 'soft'
  | { type: 'custom', name: string }
