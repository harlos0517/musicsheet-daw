/** qn stand for quarter note. ticks don't exist until tpq does. */

import type { ArticulationType, DynamicType } from '@/core/types/MusicData'
import type { CurveParameter, CurveType } from '@/core/types/MusicEvent'

export default interface InterpretationProfile {
  id: string
  name: string
  extends?: string // parent profile ID
  initialDynamics?: number // 0 - 1
  dynamics?: DynamicMapping[] // merge by type
  hairpin?: {
    cresc?: CurveShape // default: linear
    dim?: CurveShape // default: linear
    defaultChange: number // when no dynamic ends it
  }
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
  | { kind: 'any' }

export type NoteRuleCondition = {
  maxNotatedQn?: number
  minNotatedQn?: number
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
