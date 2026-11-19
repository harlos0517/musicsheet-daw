/**
 * The rules a layout is computed by. Page sizes are in millimetres; every other length
 * is in staff spaces (sp, the distance between two staff lines), so a profile works at
 * any staff size.
 */

export default interface LayoutProfile {
  id: string
  name: string
  extends?: string // parent profile ID; fields set here replace the parent's
  page: {
    widthMm: number
    heightMm: number
    margins: { top: number, bottom: number, left: number, right: number } // mm
  }
  staffSpaceMm: number // the staff size
  spacing: {
    quarterSp: number // space given to a quarter note
    durationRatio: number // space grows by this for each doubling of duration, e.g. 1.4
    minNoteSp: number // no note gets less, however short
    measurePaddingSp: number // after the barline, before the first note
  }
  distances: {
    staffSp: number // between staves of one track (piano)
    trackSp: number // between tracks
    systemSp: number // between systems
    firstIndentSp: number // first system, room for full instrument names
  }
  breaking: {
    lastSystemFillMin: number // 0 - 1; the last system is stretched to the full width
    // only when its notes take at least this share of it
  }
  placement: Partial<Record<PlacedKind, 'above' | 'below'>> // default side of the staff
  fonts: { music: string, text: string }
  padding: {
    collisionSp: number // the least gap between two items pushed apart
  }
}

export type PlacedKind = 'dynamic' | 'hairpin' | 'tempo' | 'text' | 'technique' | 'pedal'
