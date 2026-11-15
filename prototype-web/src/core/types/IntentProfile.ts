import type { ArticulationType, DynamicType } from '@/core/types/MusicData'
import type { CurveFor, CurveType } from '@/core/types/MusicEvent'

export default interface IntentProfile {
  id: string
  name: string
  extends?: string // parent profile ID
  dynamics?: DynamicMapping[] // merge by type
  hairpin?: {
    curve: CurveType
    a?: number // how intense the curve is
    defaultChange: number // when no dynamic ends it
  }
  elementRules: ElementRule[]
  curveRules: CurveRule[]
}

export interface DynamicMapping {
  type: DynamicType
  mode: ChangeMode
  value: number // sets peak value
  changeInTicks?: number // default (or 0) is off: do not change
  changeMode?: ChangeMode
  changeValue?: number // against value before
  changeCurve?: CurveType // from peak value
}

export interface ElementRule {
  id: string // a child profile replaces a parent rule with the same ID
  enabled?: boolean // a child can switch a parent's rule off
  match: Match
  when?: Condition // optional refinement
  actions: Action[]
}

export interface CurveRule {
  id: string
  enabled?: boolean
  match:
    | { kind: 'technique', techniqueId: string }
    | { kind: 'pedal', type: 'sustain' | 'sostenuto' | 'una-corda' }
  curveFor: CurveFor
  startValue?: number // 0 ~ 1, defaults to current value
  endValue?: number // 0 ~ 1, defaults to next value or 1
  points: { // describes how the curve goes with a set of (time, to) points
    time: number // 0 ~ 1, 0 at curve start and 1 at curve end
    to: number // 0 ~ 1, 0 is start value and 1 is end value
    curveType?: CurveType
    a?: number // how intense the curve is
  }[]
  // a point at time 0 jumps to that value at curve start and curveType has no effect
  // for technique that has scope 'until-next', only time-0 point is allowed.
}

export type Match =
  | { kind: 'articulation', type: ArticulationType }
  | { kind: 'technique', techniqueId: string } // from TechniqueMarking / CustomSpanner
  | { kind: 'slur', position?: 'first' | 'inner' | 'last' }
  | { kind: 'grace', slash?: boolean }
  | { kind: 'any' }

export type Condition = {
  maxNotatedTicks?: number
  minNotatedTicks?: number
  pass?: number[]
}

export type ChangeMode =  'set' | 'add' | 'scale'

export type Action =
  | { op: ChangeMode, for: 'duration' | 'velocity', value: number }
  | { op: 'startOffset', ticks: number } // does not move noteEnd
  | { op: 'addTechnique', techniqueId: string } // chaining techniques
  | { op: 'removeTechnique', techniqueId: string } // override technique
