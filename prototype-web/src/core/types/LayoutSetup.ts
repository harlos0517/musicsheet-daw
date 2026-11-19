/**
 * Everything layout needs besides the score itself. Layout never goes into MusicData:
 * the score and each part lay out the same music, each in its own way.
 *
 * Pages do not move while the music is edited. Where systems and pages break, and
 * where each system sits on its page, is kept in `flow`, the result of the last reflow;
 * edits only respace the notes inside a system. A reflow runs only when it is asked
 * for, and computes `flow` again from the profile and the layout elements.
 */
import type LayoutProfile from '@/core/types/LayoutProfile'

export interface LayoutSetup {
  layoutProfiles: LayoutProfile[]
  layouts: Layout[]
}

/** The full score, or one part. */
export interface Layout {
  id: string // 'score', 'part-tpt1'
  name: string
  layoutProfileId: string
  trackIds?: string[] // the tracks it shows, in score order; default: every track
  elements: LayoutElement[] // what the user set by hand; every reflow keeps them
  flow?: Flow // none yet: the first render reflows
}

/**
 * Where the last reflow broke the music, and where it put each system. A system holds
 * the measures from its start up to the next system's start. A measure added since
 * goes into the system of the measure before it, which may then be too full: it is
 * squeezed until the next reflow. When a system's start measure is deleted, the system
 * starts at the measure after it.
 */
export interface Flow {
  pages: FlowPage[]
}

export interface FlowPage {
  systems: FlowSystem[]
}

export interface FlowSystem {
  startMeasureId: string
  ySp: number // top of its first staff, from the page's top margin
  staffYSp: Record<string, number> // staff ID → top of the staff, from the system's top;
  // a staff not listed (one added since) goes below the last one
}

interface LayoutElementBase {
  id: string
  kind: string
  nodeId: string // the MusicData node it belongs to; dropped when the node is deleted
}

/** A system or page break after this measure, kept by every reflow. */
export interface BreakElement extends LayoutElementBase {
  kind: 'break'
  type: 'system' | 'page'
}

/** This measure stays on the same system as the next one. */
export interface NoBreakElement extends LayoutElementBase {
  kind: 'no-break'
}

/** This measure gets more or less room than its notes ask for. */
export interface StretchElement extends LayoutElementBase {
  kind: 'stretch'
  factor: number // 1 leaves it as it is
}

/** Moves a drawn item away from where the layout put it, in sp. */
export interface OffsetElement extends LayoutElementBase {
  kind: 'offset'
  x: number
  y: number // down is positive
  part?: string // which part of the node: 'accidental', 'stem', 'dot', 'number' (tuplet);
  // default: all of it
  segment?: number // a spanner cut by system breaks: which piece, from 0; default: every
  // piece. Ignored when the spanner has no such piece after a reflow
}

/** Which side of the staff, or of the notes, an item goes on. */
export interface PlacementElement extends LayoutElementBase {
  kind: 'placement'
  side: 'above' | 'below' // slur, tie, tuplet, dynamic, articulation, text
}

export interface StemElement extends LayoutElementBase {
  kind: 'stem'
  direction: 'up' | 'down'
}

/** Extra room below a staff, on the systems from this measure on, kept by reflows. */
export interface StaffGapElement extends LayoutElementBase {
  kind: 'staff-gap'
  staffId: string
  extraSp: number
}

/** Not drawn: a rest in a second voice, a courtesy accidental. Still played. */
export interface HiddenElement extends LayoutElementBase {
  kind: 'hidden'
  part?: string // as in OffsetElement; default: all of it
}

export type LayoutElement =
  | BreakElement
  | NoBreakElement
  | StretchElement
  | OffsetElement
  | PlacementElement
  | StemElement
  | StaffGapElement
  | HiddenElement
