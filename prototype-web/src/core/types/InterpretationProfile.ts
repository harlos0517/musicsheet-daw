/** qn stand for quarter note. ticks don't exist until tpq does. */

import type { ArticulationType, DynamicType } from '@/core/types/MusicData'
import type { CurveParameter, CurveType, SlurPosition } from '@/core/types/MusicEvent'

export default interface InterpretationProfile {
  id: string
  name: string
  extends?: string // parent profile ID
  initialDynamics?: number // 0 - 1
  defaultPhrasing?: 'detached' | 'connected' // default: detached
  swell?: { // a small cresc. into and dim. out of long notes and slow slurs, not written
    minMs: number // only notes played longer than this
    slurMinMs?: number // a slur rises only from a first note written longer than this,
    // and falls only into a last note that is; default: minMs. Set it to where a slur
    // counts as a fast run, so that slow notes swell and fast runs do not
    startLevel: number // fraction of the written dynamic it starts and ends at, e.g. 0.75
    noteRatio: number // share of a single note the rise and the fall each take
    slurRatio: number // share of the first note (rise) and the last note (fall) of a slur
    rise?: CurveShape // default: linear
    fall?: CurveShape // default: linear
  }
  // connected: an unbroken run of single notes in a voice is played as if slurred.
  // A run breaks at a rest, a staccato note, an accent, a repeated pitch, a chord,
  // and where a written slur begins or ends.
  dynamics?: DynamicMapping[] // merge by type
  hairpin?: {
    cresc?: CurveShape // default: linear
    dim?: CurveShape // default: linear
    defaultChange: number // when no dynamic ends it
  }
  expandTremolo?: { // write measured tremolos out as repeated notes
    unmeasuredMs?: number // also unmeasured ones (patches that cannot roll), each note
    // about this long; default: an unmeasured tremolo stays one note, a roll
  } // default: every tremolo stays one note, for the patch to play
  graceNotes?: {
    appoggiatura: {
      placement?: 'before-beat' | 'on-beat' // default: on-beat
      ratio: number // replaces how much main note with ratio of notated grace notes
    }
    acciaccatura: { // slashed
      placement?: 'before-beat' | 'on-beat' // default: before-beat
      ratio: number // prepend how much with ratio of notated grace notes
    }
  }
  noteRules: NoteRule[]
  curveRules: CurveRule[]
}

export interface DynamicMapping {
  type: DynamicType
  mode: ChangeMode
  combine?: 'add' | 'replace' // defaults to replace
  value: number // sets peak value
  then?: {
    afterQn: number
    mode: ChangeMode
    value: number // against value before
    curveType: CurveType // from peak value
  }
}

export interface NoteRule {
  id: string // a child profile replaces a parent rule with the same ID
  enabled?: boolean // a child can switch a parent's rule off
  match: NoteRuleMatch
  when?: NoteRuleCondition // optional refinement
  actions: NoteRuleAction[]
}

export interface CurveRule {
  id: string
  enabled?: boolean
  match:
    | { kind: 'technique', techniqueId: string }
    | { kind: 'pedal', type: 'sustain' | 'sostenuto' | 'una-corda' }
  parameter: CurveParameter
  startValue?: number // 0 ~ 1, defaults to current value
  endValue?: number // 0 ~ 1, defaults to next value or 1
  points: { // describes how the curve goes with a set of (time, to) points
    time: number // 0 ~ 1, 0 at curve start and 1 at curve end
    value: number // 0 ~ 1, 0 is start value and 1 is end value
    curveType?: CurveType
    steepness?: number // how intense the curve is
  }[]
  // a point at time 0 jumps to that value at curve start and curveType has no effect
  // for technique that has scope 'until-next', only time-0 point is allowed.
}

export type NoteRuleMatch =
  | { kind: 'articulation', type: ArticulationType }
  | { kind: 'technique', techniqueId: string } // from TechniqueMarking / CustomSpanner
  | { kind: 'slur', position?: 'first' | 'inner' | 'last' }
  | { kind: 'grace', slash?: boolean }
  | { kind: 'dynamic', type: DynamicType } // a dynamic written on the note, e.g. sfp
  | { kind: 'any' }

export type NoteRuleCondition = {
  maxNotatedQn?: number
  minNotatedQn?: number
  maxPreviousNotatedQn?: number // the note right before it in the same voice
  // the same two in real time, at the tempo the note is played in:
  maxNotatedMs?: number
  maxPreviousNotatedMs?: number
  slurPosition?: SlurPosition
  hairpin?: 'cresc' | 'dim' // a hairpin of this kind runs over the note
  passes?: number[]
}

export interface CurveShape {
  curveType: CurveType
  steepness?: number // see CurveType
}

export type ChangeMode =  'set' | 'add' | 'scale'

export type NoteRuleAction =
  // duration unit is quarter note
  | { op: 'add' | 'scale' | 'setQn' | 'setRatio', for: 'duration', value: number }
  | { op: ChangeMode, for: 'velocity', value: number }
  | { op: 'startOffset', qn: number } // does not move noteEnd
  | { op: 'addTechnique', techniqueId: string } // chaining techniques
  | { op: 'removeTechnique', techniqueId: string } // override technique
  | { op: 'rollAttack', qn?: number, together: boolean } // a roll (an unmeasured tremolo
  // that stays one note) starts with a single stroke of qn: played with it (together),
  // or before it, the roll coming in after; without qn, the stroke rings as long as the
  // roll (together only)
