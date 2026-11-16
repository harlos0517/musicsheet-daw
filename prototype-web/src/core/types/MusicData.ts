export enum DurationType {
  LONGA = 4,
  DOUBLE = 3,
  WHOLE = 2,
  HALF = 1,
  QUARTER = 0,
  _8TH = -1,
  _16TH = -2,
  _32ND = -3,
  _64TH = -4,
  _128TH = -5,
}

export enum ArticulationType {
  ACCENT = 'accent',
  TENUTO = 'tenuto',
  STACCATO = 'staccato',
  STACCATISSIMO = 'staccatissimo',
}

export enum ClefType {
  G = 'g',
  F = 'f',
  C = 'c',
  PERCUSSION = 'percussion',
}

export enum Step {
  C = 'c',
  D = 'd',
  E = 'e',
  F = 'f',
  G = 'g',
  A = 'a',
  B = 'b',
}

export enum BarlineStyle {
  SINGLE = 'single',
  DOUBLE = 'double',
  DASHED = 'dashed',
  FINAL = 'final',
}

export default interface MusicData {
  title: string
  composer: string
  tpq: number
  measures: Measure[]
  tracks: Track[]
  globalSpanners: GlobalSpanner[]
}

export interface MusicDataNode {
  id: string
  kind: string
}

export interface Measure extends MusicDataNode {
  kind: 'measure'
  timeSig?: TimeSignature
  key?: KeySignature
  tempos: Tempo[]
  actualTicks?: number

  // repeats
  repeatStart?: boolean
  repeatEnd?: { times: number } // total times played, default 2
  barline?: BarlineStyle
  markers: Marker[]
  jumps: Jump[]
}

export interface Track extends MusicDataNode {
  kind: 'track'
  instrumentName: string
  instrumentAbbr: string
  staves: Staff[]
  trackSpanners: TrackSpanner[]
  attachments: Attachment[]
  transposition?: { diatonic: number, chromatic: number, octave: number }
  // written -> sounding; B♭ clarinet: { diatonic: -1, chromatic: -2, octave: 0 }

  // percussion
  kit?: KitEntry[] // only for percussion tracks
}

export interface Staff extends MusicDataNode {
  kind: 'staff'
  measures: StaffMeasure[]
  staffSpanners: StaffSpanner[]
  lines?: number // default 5
}

export interface StaffMeasure extends MusicDataNode {
  kind: 'staff-measure'
  measureId: string
  clefs: { tick: number, clef: Clef }[]
  voices: Voice[]
  // every voice must fill the measure’s length (actualTicks, otherwise from the time signature).
}

export interface Voice extends MusicDataNode {
  kind: 'voice'
  index: number
  events: VoiceEvent[]
}

export type VoiceEvent = Chord | Rest | Space | Tuplet

export interface TimeSignature extends MusicDataNode {
  kind: 'time-signature'
  beats: number
  unit: number
}

export interface KeySignature extends MusicDataNode {
  kind: 'key-signature'
  accidentals: number // negative as flats, positive as sharps
}

export interface Duration {
  type: DurationType
  dots: number
}

export interface Tempo extends MusicDataNode {
  kind: 'tempo'
  unit: Duration
  bpm: number
  tick: number
  text?: string
}

export interface Clef extends MusicDataNode {
  kind: 'clef'
  type: ClefType
  line: number
  octaveChange: number
}

export interface Chord extends MusicDataNode {
  kind: 'chord'
  duration: Duration
  notes: (Note | UnpitchedNote)[]
  articulations: Articulation[]
  graceNotes?: GraceChords
  beam?: 'break-before' | 'join-previous' | 'none'
}

export interface GraceChords extends MusicDataNode {
  kind: 'grace-chords'
  slash?: boolean
  chords: Omit<Chord, 'graceNotes'>[]
  placement?: 'before' | 'after'
}

export interface Rest extends MusicDataNode {
  kind: 'rest'
  duration: Duration
  fullMeasure?: boolean
  beam?: 'break-before' | 'join-previous' | 'none'
}

export interface Space extends MusicDataNode { kind: 'space', duration: Duration }

export interface Tuplet extends MusicDataNode {
  kind: 'tuplet'
  unit: Duration
  normal: number
  actual: number
  events: VoiceEvent[]
}

export interface Note extends MusicDataNode {
  kind: 'note'
  pitch: Pitch
  tie?: { to: string }[] | { letRing: true } // multiple targets when crossing repeats
  accidentalDisplay?: 'show' | 'hide' | 'cautionary'
}

export interface Pitch {
  step: Step
  alter: number // -2..2
  octave: number
}

export interface Articulation extends MusicDataNode {
  kind: 'articulation'
  type: ArticulationType
}

export type ElementAnchor = {
  type: 'element'
  id: string // chord, rest, or note
}

export type TickAnchor = {
  type: 'tick'
  measureId: string // free position in a bar
  tick: number // tick = bar length means just before the barline
}


/** REPEAT MARKS */

export interface Marker extends MusicDataNode {
  kind: 'marker'
  type: 'segno' | 'coda' | 'fine' | 'to-coda'
  label?: string // for scores with several codas
}

export interface Jump extends MusicDataNode {
  kind: 'jump'
  to: { type: 'start' } | { type: 'marker', id: string }
  until?: string // marker ID where this pass stops (fine / to-coda)
  then?: string // marker ID to continue from (coda)
  playRepeats: boolean // repeat again after the jump?
  displayText?: string // e.g. "D.S. al Coda"
}

export interface Volta extends MusicDataNode {
  kind: 'volta'
  startMeasureId: string
  endMeasureId: string
  passes: number[]                // [1], [2], [1, 2, 3]…
  openEnd?: boolean               // drawn without a closing hook
}


/** SPANNERS */

export interface Slur extends MusicDataNode {
  kind: 'slur'
  start: ElementAnchor
  end: ElementAnchor // always include end
}

export interface Hairpin extends MusicDataNode {
  kind: 'hairpin'
  start: ElementAnchor | TickAnchor
  end: ElementAnchor | TickAnchor
  includeEnd?: boolean // default: false
  staffIds?: string[] // default: all staves of the track
  type: 'cresc' | 'dim'
  niente?: boolean
}

export interface Ottava extends MusicDataNode {
  kind: 'ottava'
  start: ElementAnchor
  end: ElementAnchor // always include end
  shift: -2 | -1 | 1 | 2
}

export interface Pedal extends MusicDataNode {
  kind: 'pedal'
  type: 'sustain' | 'sostenuto' | 'una-corda'
  start: TickAnchor
  end: TickAnchor
  includeEnd?: boolean // default: false
}

export interface CustomSpanner extends MusicDataNode {
  kind: 'custom-spanner'
  staffIds?: string[] // default: all staves of the track
  voiceIndexes?: number[] // default: all voices
  start: ElementAnchor | TickAnchor
  end: ElementAnchor | TickAnchor
  includeEnd?: boolean // default: false
  techniqueId: string
  text?: string
  passes?: number[]
}

export type StaffSpanner = Ottava
export type TrackSpanner = Slur | Hairpin | Pedal | CustomSpanner

export interface TempoChange extends MusicDataNode { // score-wide
  kind: 'tempo-change'
  start: TickAnchor
  end: TickAnchor
  type: 'rit' | 'accel'
  targetTempo?: number // optional; otherwise the playback profile decides
}

export type GlobalSpanner = Volta | TempoChange


/** DYNAMICS & TEXT */

interface AttachmentBase extends MusicDataNode {
  staffIds?: string[] // default: all staves of the track
  voiceIndexes?: number[] // default: all voices
  beforeTick?: boolean // default: false
  // placed at the end of the previous event (e.g. the dynamic where a hairpin ends),
  // not on the chord at this tick.
}

export interface Dynamic extends AttachmentBase {
  at: ElementAnchor | TickAnchor
  kind: 'dynamic'
  type: DynamicType // 'ppp'...'fff', 'sfz', 'fp', 'sf', 'rfz'…
  text?: string // custom display, e.g. "sub. p", "mf espress."
}

export enum DynamicType {
  PPPP = 'pppp',
  PPP = 'ppp',
  PP = 'pp',
  P = 'p',
  MP = 'mp',
  MF = 'mf',
  F = 'f',
  FF = 'ff',
  FFF = 'fff',
  FFFF = 'ffff',
  SFZ = 'sfz',
  FP = 'fp',
  SF = 'sf',
  RFZ = 'rfz',
}

export type TechniqueMarking = AttachmentBase & {
  kind: 'technique'
  techniqueId: string // registry ID: 'core:pizz', 'user:sul-pont'
  text?: string // display, e.g. "pizz."
  passes?: number[]
} & (
  | { at: ElementAnchor, scope: 'this' | 'until-next' }
  | { at: TickAnchor, scope: 'until-next' }
)

export type Attachment = Dynamic | TechniqueMarking


/** PERCUSSION */

export interface KitEntry extends MusicDataNode {
  kind: 'kit-entry'
  name: string // "Snare", "Closed Hi-hat"
  sound: string // e.g. 'snare', 'hihat-closed': the Instrument Profile maps this to a MIDI note
  displayPitch: number // relative to percussion clef center
  notehead: 'normal' | 'x' | 'circle-x' | 'triangle' | 'diamond'
}

export interface UnpitchedNote extends MusicDataNode {
  kind: 'unpitched-note'
  kitEntryId: string
  tie?: { to: string }[] | { letRing: true } // multiple targets when crossing repeats
}
