export interface MusicEventBase {
  kind: string
  id: string // ${sourceId}@${pass}#${segmentIndex}
  sourceIds: string[] // notes or elements it came from (could be several)
  trackId: string
}

export interface NoteEvent extends MusicEventBase {
  kind: 'note-event'
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

export interface CurveEvent extends MusicEventBase {
  // overlapping curve for the same parameter shouldn't be legal;
  // if that ever happens, the latter wins.
  // value holds between curve segments.
  kind: 'curve-event'
  pass: number
  segmentIndex: number
  parameter: CurveParameter
  // curve could be tied to a technique group with continuous values
  appliesTo?: { staffId: string, voiceIndex?: number }[]
  combine?: 'add' | 'replace' // defaults to replace
  startTick: number
  durationTicks: number
  fromValue: number // 0 ~ 1
  toValue: number // 0 ~ 1
  curveType?: CurveType // when duration is 0, this is ignored and 'hold' is used
  steepness?: number // how intense the curve is
}

export type CurveType = 'hold' | 'linear' | 'exp' | 'power' | 'ease'
// hold: holds start value and change instantly at end
// the meaning of steepness (k) for each curvetype:
// hold/linear: no effect
// exp: (e^(kt) − 1) / (e^k − 1)
// power: k > 0: t^(1+k); k < 0: 1 − (1−t)^(1−k)
// ease: the power curve on each half, mirrored at the middle

export type MusicEvent = NoteEvent | CurveEvent

export type SlurPosition = 'first' | 'inner' | 'last'

export type CurveParameter =
  | 'dynamics' | 'sustain' | 'sostenuto' | 'una-corda'
  | { kind: 'custom', name: string }
