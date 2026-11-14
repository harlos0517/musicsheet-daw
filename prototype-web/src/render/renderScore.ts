/**
 * Draws MusicData with VexFlow: a simple layout strategy (fill each system greedily,
 * then justify) plus drawing of everything VexFlow doesn't place by itself.
 *
 * Layout Data isn't designed yet, so system breaks are computed here and nothing is stored.
 */
import {
  BarlineType,
  Curve,
  Formatter,
  type RenderContext,
  Renderer,
  Repetition,
  Stave,
  StaveConnector,
  StaveTie,
  TextBracket,
  type Note as VFNote,
  VoltaType,
} from 'vexflow/bravura'

import { transposeKey } from '@/core/pitch'
import { type MeasureInfo, measureTimeline, timeVoice } from '@/core/time'
import type MusicData from '@/core/types/MusicData'
import type {
  Clef,
  ElementAnchor,
  Measure,
  Staff,
  TickAnchor,
  Track,
} from '@/core/types/MusicData'
import { BarlineStyle, ClefType } from '@/core/types/MusicData'

import {
  type BuiltStaffMeasure,
  type NoteRef,
  buildStaffMeasure,
  vfClef,
  vfDuration,
} from './notes'

export interface RenderOptions {
  width: number
}

export interface RenderResult {
  /** Things that could not be drawn; the rest of the score still renders. */
  warnings: string[]
}

/* ---------- layout constants (px) ---------- */

const MARGIN_RIGHT = 20
const NAME_GAP = 16 // between the instrument name and the system start
const TITLE_HEIGHT = 90
const STAFF_STEP = 100 // stave y → next stave y within a track
const TRACK_STEP = 120 // stave y → next stave y across tracks
const SYSTEM_PADDING_TOP = 30
const SYSTEM_PADDING_BOTTOM = 70
const MEASURE_PADDING = 30

const KEY_NAMES: Record<number, string> = {
  [-7]: 'Cb', [-6]: 'Gb', [-5]: 'Db', [-4]: 'Ab', [-3]: 'Eb', [-2]: 'Bb', [-1]: 'F',
  0: 'C', 1: 'G', 2: 'D', 3: 'A', 4: 'E', 5: 'B', 6: 'F#', 7: 'C#',
}

/** SMuFL glyphs (Bravura) for dynamic letters and pedal marks. */
const DYNAMIC_GLYPHS: Record<string, string> = {
  p: '', m: '', f: '', r: '', s: '', z: '', n: '',
}
const PEDAL_DOWN = ''
const PEDAL_UP = ''

/** Fonts must be ready before VexFlow measures anything. */
export const loadScoreFonts = async(): Promise<void> => {
  await Promise.all([document.fonts.load('30px Bravura'), document.fonts.load('12px Academico')])
}

interface StaffEntry {
  track: Track
  trackIndex: number
  staff: Staff
  staffIndex: number
  lines: number
}

interface SystemLayout {
  measures: number[]
  widths: number[]
  x: number
  y: number
  /** Stave y per staff entry. */
  staffY: number[]
  height: number
  startTick: number
  endTick: number
}

interface DrawnSystem {
  layout: SystemLayout
  /** Per staff ID: the stave of each measure, in order. */
  staves: Map<string, Stave[]>
  /** Per staff ID: time → x points, sorted by tick. */
  points: Map<string, { tick: number, x: number }[]>
  /** Per staff ID: rendered notes in order (for splitting spanners across systems). */
  notes: Map<string, VFNote[]>
}

export const renderScore = (
  element: HTMLDivElement,
  music: MusicData,
  options: RenderOptions,
): RenderResult => {
  const warnings: string[] = []
  const { tpq } = music
  const width = Math.max(500, options.width)
  const timeline = measureTimeline(music)

  const entries: StaffEntry[] = music.tracks.flatMap((track, trackIndex) =>
    track.staves.map((staff, staffIndex) =>
      ({ track, trackIndex, staff, staffIndex, lines: staff.lines ?? 5 })))

  // Left margin: room for the longest instrument name (full names first, abbreviations after).
  const braceRoom = music.tracks.some(t => t.staves.length > 1) ? 14 : 0
  const nameMargin = (names: string[]) =>
    10 + braceRoom + NAME_GAP + Math.max(0, ...names.map(n => textWidth(n, 11)))
  const firstMargin = nameMargin(music.tracks.map(t => t.instrumentName))
  const laterMargin = nameMargin(music.tracks.map(t => t.instrumentAbbr))

  /* ---------- indexes ---------- */

  // Absolute tick and staff of every chord, rest and note.
  const elementTicks = new Map<string, { tick: number, ticks: number, staffId: string }>()
  const tieTargets = new Set<string>()
  for (const { staff } of entries) {
    staff.measures.forEach((staffMeasure, m) => {
      const info = timeline[m]
      if (!info) return
      for (const voice of staffMeasure.voices) {
        for (const { event, tick, ticks } of timeVoice(voice.events, tpq, info.startTick)) {
          if (event.kind === 'tuplet' || event.kind === 'space') continue
          elementTicks.set(event.id, { tick, ticks, staffId: staff.id })
          if (event.kind !== 'chord') continue
          for (const n of event.notes) {
            elementTicks.set(n.id, { tick, ticks, staffId: staff.id })
            if (n.tie && 'to' in n.tie) tieTargets.add(n.tie.to)
          }
        }
      }
    })
  }

  const anchorTick = (anchor: ElementAnchor | TickAnchor): number | undefined => {
    if (anchor.type === 'element') return elementTicks.get(anchor.id)?.tick
    const info = timeline.find(i => i.measure.id === anchor.measureId)
    return info ? info.startTick + anchor.tick : undefined
  }

  // Clef changes per staff, as absolute ticks.
  const clefChanges = new Map<string, { tick: number, clef: Clef }[]>()
  for (const { staff } of entries) {
    clefChanges.set(staff.id, staff.measures.flatMap((sm, m) =>
      sm.clefs.map(c => ({ tick: (timeline[m]?.startTick ?? 0) + c.tick, clef: c.clef }))))
  }
  const fallbackClef: Clef = { id: '', kind: 'clef', type: ClefType.G, line: 2, octaveChange: 0 }
  const clefAt = (staffId: string, tick: number): Clef => {
    let current = fallbackClef
    for (const change of clefChanges.get(staffId) ?? [])
      if (change.tick <= tick) current = change.clef
    return current
  }

  // 8va ranges per staff (end includes the end note).
  const ottavas = new Map<string, { start: number, end: number, shift: number }[]>()
  for (const { staff } of entries) {
    ottavas.set(staff.id, staff.staffSpanners.flatMap(o => {
      const start = elementTicks.get(o.start.id)
      const end = elementTicks.get(o.end.id)
      return start && end ? [{ start: start.tick, end: end.tick + end.ticks, shift: o.shift }] : []
    }))
  }
  const ottavaAt = (staffId: string, tick: number) =>
    ottavas.get(staffId)?.find(o => tick >= o.start && tick < o.end)?.shift ?? 0

  const writtenKey = (track: Track, info: MeasureInfo) => {
    if (track.kit) return 0
    const concert = info.key.accidentals
    return track.transposition ? transposeKey(concert, track.transposition, -1) : concert
  }

  /* ---------- building ---------- */

  const buildColumn = (m: number): (BuiltStaffMeasure | undefined)[] => {
    const info = timeline[m]!
    return entries.map(({ track, staff, lines }) => {
      const staffMeasure = staff.measures[m]
      if (!staffMeasure) return undefined
      try {
        return buildStaffMeasure(staffMeasure, {
          tpq,
          lines,
          writtenKey: writtenKey(track, info),
          measureStartTick: info.startTick,
          lengthTicks: info.lengthTicks,
          timeSig: info.timeSig,
          clefAt: tick => clefAt(staff.id, tick),
          ottavaAt: tick => ottavaAt(staff.id, tick),
          transposition: track.transposition,
          kit: track.kit,
          tieTargets,
        })
      } catch(e) {
        warnings.push(`measure ${m + 1}, ${track.instrumentName}: ${(e as Error).message}`)
        return undefined
      }
    })
  }

  /** Adds clef / key / time signature to a stave; returns nothing, the stave measures itself. */
  const addModifiers = (stave: Stave, entry: StaffEntry, m: number, systemStart: boolean) => {
    const info = timeline[m]!
    const measure = info.measure
    const startClef = entry.staff.measures[m]?.clefs.find(c => c.tick === 0)
    if (systemStart) {
      const { name, annotation } = vfClef(clefAt(entry.staff.id, info.startTick))
      stave.addClef(name, 'default', annotation)
      if (!entry.track.kit) stave.addKeySignature(KEY_NAMES[writtenKey(entry.track, info)] ?? 'C')
      if (m === 0 || measure.timeSig)
        stave.addTimeSignature(`${info.timeSig.beats}/${info.timeSig.unit}`)
      return
    }
    if (startClef) {
      const { name, annotation } = vfClef(startClef.clef)
      stave.addClef(name, 'small', annotation)
    }
    const previous = timeline[m - 1]
    if (measure.key && previous && !entry.track.kit) {
      const key = KEY_NAMES[writtenKey(entry.track, info)] ?? 'C'
      stave.addKeySignature(key, KEY_NAMES[writtenKey(entry.track, previous)])
    }
    if (measure.timeSig) stave.addTimeSignature(`${measure.timeSig.beats}/${measure.timeSig.unit}`)
  }

  const modifierWidth = (m: number, systemStart: boolean): number =>
    Math.max(0, ...entries.map(entry => {
      const bare = new Stave(0, 0, 1000)
      const stave = new Stave(0, 0, 1000)
      addModifiers(stave, entry, m, systemStart)
      return stave.getNoteStartX() - bare.getNoteStartX()
    }))

  const minNoteWidth = (m: number): number => {
    const built = buildColumn(m).filter((b): b is BuiltStaffMeasure => !!b)
    const voices = built.flatMap(b => b.voices)
    if (voices.length === 0) return 60
    const formatter = new Formatter()
    built.forEach(b => formatter.joinVoices(b.voices))
    try {
      return formatter.preCalculateMinTotalWidth(voices) + MEASURE_PADDING
    } catch(e) {
      warnings.push(`measure ${m + 1}: ${(e as Error).message}`)
      return 100
    }
  }

  /* ---------- layout: systems ---------- */

  const noteWidths = timeline.map((_, m) => minNoteWidth(m))
  const startMods = timeline.map((_, m) => modifierWidth(m, true))
  const midMods = timeline.map((_, m) => modifierWidth(m, false))

  const systemsMeasures: number[][] = []
  {
    let current: number[] = []
    let used = 0
    timeline.forEach((_, m) => {
      const margin = systemsMeasures.length === 0 ? firstMargin : laterMargin
      const available = width - MARGIN_RIGHT - margin
      const cost = noteWidths[m]! + (current.length === 0 ? startMods[m]! : midMods[m]!)
      if (current.length > 0 && used + cost > available) {
        systemsMeasures.push(current)
        current = [m]
        used = noteWidths[m]! + startMods[m]!
      } else {
        current.push(m)
        used += cost
      }
    })
    if (current.length > 0) systemsMeasures.push(current)
  }

  // Vertical positions of staves inside a system.
  const staffOffsets: number[] = []
  {
    let y = SYSTEM_PADDING_TOP
    entries.forEach((entry, i) => {
      if (i > 0) y += entry.staffIndex === 0 ? TRACK_STEP : STAFF_STEP
      staffOffsets.push(y)
    })
  }
  const systemHeight = (staffOffsets.at(-1) ?? 0) + 40 + 40 + SYSTEM_PADDING_BOTTOM

  const layouts: SystemLayout[] = systemsMeasures.map((measures, s) => {
    const x = s === 0 ? firstMargin : laterMargin
    const available = width - MARGIN_RIGHT - x
    const base = measures.map((m, i) => noteWidths[m]! + (i === 0 ? startMods[m]! : midMods[m]!))
    const total = base.reduce((a, b) => a + b, 0)
    const notes = measures.reduce((a, m) => a + noteWidths[m]!, 0)
    const isLast = s === systemsMeasures.length - 1
    let extra = Math.max(0, available - total)
    if (isLast && total < available * 0.6) extra = Math.min(extra, notes * 0.4)
    const widths = base.map((w, i) => w + extra * noteWidths[measures[i]!]! / notes)
    const y = TITLE_HEIGHT + s * systemHeight
    const first = timeline[measures[0]!]!
    const last = timeline[measures.at(-1)!]!
    return {
      measures,
      widths,
      x,
      y,
      staffY: staffOffsets.map(o => y + o),
      height: systemHeight,
      startTick: first.startTick,
      endTick: last.startTick + last.lengthTicks,
    }
  })

  const height = TITLE_HEIGHT + layouts.length * systemHeight + 20

  /* ---------- drawing ---------- */

  element.innerHTML = ''
  const renderer = new Renderer(element, Renderer.Backends.SVG)
  renderer.resize(width, height)
  const ctx = renderer.getContext()

  drawText(ctx, music.title, width / 2, 45, { size: 24, align: 'center', weight: 'bold' })
  drawText(ctx, music.composer, width - MARGIN_RIGHT, 75, { size: 13, align: 'right' })

  const refs = new Map<string, NoteRef & { system: number }>()
  const drawn: DrawnSystem[] = layouts.map((layout, s) =>
    drawSystem(ctx, layout, s))

  function drawSystem(context: RenderContext, layout: SystemLayout, s: number): DrawnSystem {
    const staves = new Map<string, Stave[]>()
    const points = new Map<string, { tick: number, x: number }[]>()
    const notes = new Map<string, VFNote[]>()
    entries.forEach(e => {
      staves.set(e.staff.id, [])
      points.set(e.staff.id, [])
      notes.set(e.staff.id, [])
    })

    let x = layout.x
    layout.measures.forEach((m, i) => {
      const info = timeline[m]!
      const measureWidth = layout.widths[i]!
      const built = buildColumn(m)
      const columnStaves = entries.map((entry, e) => {
        const stave = new Stave(x, layout.staffY[e]!, measureWidth, { numLines: entry.lines })
        addModifiers(stave, entry, m, i === 0)
        const endClef = built[e]?.endClef
        if (endClef) {
          const { name, annotation } = vfClef(endClef)
          stave.setEndClef(name, 'small', annotation)
        }
        setBarlines(stave, info.measure)
        if (e === 0) decorateTopStave(stave, m, i === 0)
        stave.setContext(context)
        return stave
      })

      const noteStart = Math.max(...columnStaves.map(st => st.getNoteStartX()))
      columnStaves.forEach(st => st.setNoteStartX(noteStart))

      const formatter = new Formatter()
      const voices = built.flatMap(b => b?.voices ?? [])
      built.forEach(b => b && formatter.joinVoices(b.voices))
      columnStaves.forEach(st => st.draw())
      try {
        if (voices.length > 0) {
          const justify = columnStaves[0]!.getNoteEndX() - noteStart - 10
          formatter.format(voices, Math.max(20, justify))
        }
        built.forEach((b, e) => {
          if (!b) return
          const stave = columnStaves[e]!
          b.voices.forEach(v => v.draw(context, stave))
          b.beams.forEach(beam => beam.setContext(context).draw())
          b.tuplets.forEach(tuplet => tuplet.setContext(context).draw())
        })
      } catch(err) {
        warnings.push(`measure ${m + 1}: ${(err as Error).message}`)
      }

      built.forEach((b, e) => {
        const id = entries[e]!.staff.id
        const stave = columnStaves[e]!
        staves.get(id)!.push(stave)
        const list = points.get(id)!
        list.push({ tick: info.startTick, x: stave.getNoteStartX() })
        b?.points.forEach(p =>
          list.push({ tick: p.tick, x: safeX(p.note) ?? stave.getNoteStartX() }))
        list.push({ tick: info.startTick + info.lengthTicks, x: stave.getX() + stave.getWidth() })
        b?.points.forEach(p => notes.get(id)!.push(p.note))
        b?.refs.forEach((ref, key) => refs.set(key, { ...ref, system: s }))
      })

      // Barlines through the staves of multi-staff instruments.
      const connect = (top: Stave, bottom: Stave, type: 'singleRight' | 'singleLeft' | 'brace') =>
        new StaveConnector(top, bottom).setType(type).setContext(context).draw()
      forEachTrackGroup((top, bottom) =>
        connect(columnStaves[top]!, columnStaves[bottom]!, 'singleRight'))
      if (i === 0) {
        if (entries.length > 1) connect(columnStaves[0]!, columnStaves.at(-1)!, 'singleLeft')
        forEachTrackGroup((top, bottom) =>
          connect(columnStaves[top]!, columnStaves[bottom]!, 'brace'))
        drawInstrumentNames(context, columnStaves, s === 0)
      }
      x += measureWidth
    })

    for (const list of points.values()) list.sort((a, b) => a.tick - b.tick || a.x - b.x)
    return { layout, staves, points, notes }
  }

  function forEachTrackGroup(callback: (top: number, bottom: number) => void) {
    music.tracks.forEach(track => {
      if (track.staves.length < 2) return
      const top = entries.findIndex(e => e.staff.id === track.staves[0]!.id)
      callback(top, top + track.staves.length - 1)
    })
  }

  function drawInstrumentNames(context: RenderContext, columnStaves: Stave[], first: boolean) {
    music.tracks.forEach(track => {
      const top = entries.findIndex(e => e.staff.id === track.staves[0]!.id)
      const bottom = top + track.staves.length - 1
      const topY = columnStaves[top]!.getYForLine(0)
      const bottomY = columnStaves[bottom]!.getYForLine(entries[bottom]!.lines - 1)
      const y = (topY + bottomY) / 2 + 4
      const x = columnStaves[top]!.getX() - braceRoom - NAME_GAP / 2
      const name = first ? track.instrumentName : track.instrumentAbbr
      drawText(context, name, x, y, { size: 11, align: 'right' })
    })
  }

  function setBarlines(stave: Stave, measure: Measure) {
    if (measure.repeatStart) stave.setBegBarType(BarlineType.REPEAT_BEGIN)
    if (measure.repeatEnd) stave.setEndBarType(BarlineType.REPEAT_END)
    else if (measure.barline === BarlineStyle.FINAL) stave.setEndBarType(BarlineType.END)
    else if (measure.barline === BarlineStyle.DOUBLE) stave.setEndBarType(BarlineType.DOUBLE)
  }

  /** Tempo, voltas, segno/coda and jumps go on the top stave of each system. */
  function decorateTopStave(stave: Stave, m: number, systemStart: boolean) {
    const measure = timeline[m]!.measure
    const tempo = measure.tempos.find(t => t.tickInMeasure === 0)
    if (tempo) {
      stave.setTempo({
        ...(tempo.displayText !== undefined ? { name: tempo.displayText } : {}),
        duration: vfDuration(tempo.unit),
        dots: tempo.unit.dot,
        bpm: tempo.value,
      }, systemStart ? -10 : 0)
    }

    for (const spanner of music.globalSpanners) {
      if (spanner.kind !== 'volta') continue
      const a = timeline.findIndex(i => i.measure.id === spanner.startMeasureId)
      const b = timeline.findIndex(i => i.measure.id === spanner.endMeasureId)
      if (m < a || m > b) continue
      const label = `${spanner.passes.join(', ')}.`
      const lastType = spanner.openEnd ? VoltaType.MID : VoltaType.END
      let type: VoltaType = VoltaType.MID
      if (a === b) type = spanner.openEnd ? VoltaType.BEGIN : VoltaType.BEGIN_END
      else if (m === a) type = VoltaType.BEGIN
      else if (m === b) type = lastType
      stave.setVoltaType(type, m === a ? label : '', -5)
    }

    const marker = measure.markers[0]
    const jump = measure.jumps[0]
    const R = Repetition.type
    if (jump) {
      const untilType = jump.until ? findMarkerType(jump.until) : undefined
      const fromStart = jump.to.type === 'start'
      let type = fromStart ? R.DC : R.DS
      if (untilType === 'fine') type = fromStart ? R.DC_AL_FINE : R.DS_AL_FINE
      else if (untilType === 'to-coda') type = fromStart ? R.DC_AL_CODA : R.DS_AL_CODA
      stave.setRepetitionType(type, 15)
    } else if (marker) {
      const types = {
        'segno': R.SEGNO_LEFT, 'coda': R.CODA_LEFT, 'fine': R.FINE, 'to-coda': R.TO_CODA,
      }
      stave.setRepetitionType(types[marker.type], 15)
    }
  }

  function findMarkerType(id: string) {
    for (const measure of music.measures) {
      const found = measure.markers.find(mk => mk.id === id)
      if (found) return found.type
    }
    return undefined
  }

  /* ---------- things placed after notes ---------- */

  /** x for an absolute tick on a staff in a system, interpolating between notes. */
  const xAt = (system: DrawnSystem, staffId: string, tick: number): number => {
    const list = system.points.get(staffId) ?? []
    if (list.length === 0) return system.layout.x
    if (tick <= list[0]!.tick) return list[0]!.x
    for (let i = 1; i < list.length; i++) {
      const a = list[i - 1]!
      const b = list[i]!
      if (tick === b.tick) return b.x
      if (tick < b.tick) return a.x + (b.x - a.x) * (tick - a.tick) / (b.tick - a.tick)
    }
    return list.at(-1)!.x
  }

  const systemIndexAt = (tick: number) =>
    Math.max(0, drawn.findIndex(d => tick >= d.layout.startTick && tick < d.layout.endTick))

  /**
   * Splits a time range into per-system segments: [system, x1, x2].
   * `endInclusive` extends the end to cover the end element's notehead.
   */
  const segments = (
    staffId: string,
    start: ElementAnchor | TickAnchor,
    end: ElementAnchor | TickAnchor,
    endInclusive: boolean,
  ): { system: DrawnSystem, x1: number, x2: number }[] => {
    const a = anchorTick(start)
    const b = anchorTick(end)
    if (a === undefined || b === undefined) return []
    const first = systemIndexAt(a)
    const last = b >= (drawn.at(-1)?.layout.endTick ?? 0) ? drawn.length - 1 : systemIndexAt(b)
    const result: { system: DrawnSystem, x1: number, x2: number }[] = []
    for (let s = first; s <= last; s++) {
      const system = drawn[s]!
      const staves = system.staves.get(staffId) ?? []
      const lastStave = staves.at(-1)!
      const x1 = s === first
        ? anchorX(system, staffId, start, false)
        : staves[0]!.getNoteStartX() - 10
      const x2 = s === last
        ? anchorX(system, staffId, end, endInclusive)
        : lastStave.getX() + lastStave.getWidth()
      result.push({ system, x1, x2 })
    }
    return result
  }

  const anchorX = (
    system: DrawnSystem,
    staffId: string,
    anchor: ElementAnchor | TickAnchor,
    endEdge: boolean,
  ) => {
    if (anchor.type === 'element') {
      const ref = refs.get(anchor.id)
      const x = ref ? safeX(ref.note) : undefined
      if (x !== undefined) return endEdge ? x + 14 : x
    }
    return xAt(system, staffId, anchorTick(anchor) ?? 0)
  }

  const trackEntries = (track: Track) => entries.filter(e => e.track.id === track.id)

  /** y for dynamics and hairpins: below the staff, or between the staves of a piano. */
  const dynamicsY = (system: DrawnSystem, track: Track, staffIds?: string[]) => {
    const own = trackEntries(track)
    const target = staffIds?.length ? own.filter(e => staffIds.includes(e.staff.id)) : own
    const firstEntry = target[0] ?? own[0]!
    const stave = system.staves.get(firstEntry.staff.id)![0]!
    const bottom = stave.getYForLine(firstEntry.lines - 1)
    const next = own[own.indexOf(firstEntry) + 1]
    if (next && (!staffIds?.length || target.length > 1)) {
      const nextTop = system.staves.get(next.staff.id)![0]!.getYForLine(0)
      return (bottom + nextTop) / 2 + 6
    }
    return bottom + 38
  }

  const staffTopY = (system: DrawnSystem, staffId: string) =>
    system.staves.get(staffId)![0]!.getYForLine(0)
  const staffBottomY = (system: DrawnSystem, entry: StaffEntry) =>
    system.staves.get(entry.staff.id)![0]!.getYForLine(entry.lines - 1)

  const safely = (what: string, draw: () => void) => {
    try {
      draw()
    } catch(e) {
      warnings.push(`${what}: ${(e as Error).message}`)
    }
  }

  // Ties
  for (const { staff } of entries) {
    const events = staff.measures.flatMap(sm =>
      sm.voices.flatMap(v => timeVoice(v.events, tpq).map(t => t.event)))
    events.forEach(event => {
      if (event.kind !== 'chord') return
      for (const n of event.notes) {
        if (!n.tie || !('to' in n.tie)) continue
        const from = refs.get(n.id)
        const to = refs.get(n.tie.to)
        if (!from || !to) continue
        safely('tie', () => {
          if (from.system === to.system) {
            new StaveTie({
              firstNote: from.note,
              lastNote: to.note,
              firstIndexes: [from.index ?? 0],
              lastIndexes: [to.index ?? 0],
            }).setContext(ctx).draw()
          } else {
            new StaveTie({ firstNote: from.note, firstIndexes: [from.index ?? 0] })
              .setContext(ctx).draw()
            new StaveTie({ lastNote: to.note, lastIndexes: [to.index ?? 0] })
              .setContext(ctx).draw()
          }
        })
      }
    })
  }

  // Slurs and 8va lines: drawn between notes, split across systems.
  const noteSpanSegments = (staffId: string, fromId: string, toId: string) => {
    const from = refs.get(fromId)
    const to = refs.get(toId)
    if (!from || !to) return []
    if (from.system === to.system) return [{ from: from.note, to: to.note }]
    const toStaff = elementTicks.get(toId)?.staffId ?? staffId
    const fromStaff = elementTicks.get(fromId)?.staffId ?? staffId
    const pieces: { from: VFNote, to: VFNote }[] = []
    const firstSystemNotes = drawn[from.system]!.notes.get(fromStaff) ?? []
    pieces.push({ from: from.note, to: firstSystemNotes.at(-1) ?? from.note })
    const lastSystemNotes = drawn[to.system]!.notes.get(toStaff) ?? []
    pieces.push({ from: lastSystemNotes[0] ?? to.note, to: to.note })
    return pieces
  }

  music.tracks.forEach(track => {
    for (const spanner of track.trackSpanners) {
      if (spanner.kind !== 'slur') continue
      const staffId = elementTicks.get(spanner.start.id)?.staffId ?? track.staves[0]!.id
      for (const piece of noteSpanSegments(staffId, spanner.start.id, spanner.end.id))
        safely('slur', () => new Curve(piece.from, piece.to, {}).setContext(ctx).draw())
    }
    for (const staff of track.staves) {
      for (const ottava of staff.staffSpanners) {
        const labels = { [-2]: ['15', 'mb'], [-1]: ['8', 'vb'], 1: ['8', 'va'], 2: ['15', 'ma'] }
        const label = labels[ottava.shift]
        for (const piece of noteSpanSegments(staff.id, ottava.start.id, ottava.end.id)) {
          safely('8va', () => new TextBracket({
            start: piece.from,
            stop: piece.to,
            text: label[0]!,
            superscript: label[1]!,
            position: ottava.shift > 0 ? 'top' : 'bottom',
          }).setContext(ctx).draw())
        }
      }
    }
  })

  // Dynamics, technique text, hairpins, pedals
  music.tracks.forEach(track => {
    const own = trackEntries(track)
    const firstStaffId = own[0]!.staff.id

    for (const attachment of track.attachments) {
      const tick = anchorTick(attachment.at)
      if (tick === undefined) continue
      const system = drawn[systemIndexAt(tick)]!
      let x = anchorX(system, firstStaffId, attachment.at, false)
      if (attachment.beforeTick) x -= 24
      if (attachment.kind === 'dynamic') {
        const y = dynamicsY(system, track, attachment.staffIds)
        if (attachment.text !== undefined)
          drawText(ctx, attachment.text, x, y, { size: 13, style: 'italic' })
        else
          drawText(ctx, dynamicGlyphs(attachment.type), x - 4, y, { size: 30, family: 'Bravura' })
      } else {
        const staffId = attachment.staffIds?.[0] ?? firstStaffId
        const y = staffTopY(system, staffId) - 18
        const label = attachment.text ?? attachment.techniqueId
        drawText(ctx, label, x, y, { size: 12, style: 'italic' })
      }
    }

    for (const spanner of track.trackSpanners) {
      if (spanner.kind === 'slur') continue
      const pieces = (staffId: string) =>
        segments(staffId, spanner.start, spanner.end, spanner.includeEnd ?? false)
      if (spanner.kind === 'hairpin') {
        for (const { system, x1, x2 } of pieces(firstStaffId)) {
          const y = dynamicsY(system, track, spanner.staffIds) - 8
          const open = 6
          const [narrow, wide] = spanner.type === 'cresc' ? [x1 + 4, x2 - 4] : [x2 - 4, x1 + 4]
          ctx.beginPath()
          ctx.moveTo(wide, y - open)
          ctx.lineTo(narrow, y)
          ctx.lineTo(wide, y + open)
          ctx.stroke()
        }
      } else if (spanner.kind === 'pedal') {
        const bottomEntry = own.at(-1)!
        for (const { system, x1, x2 } of pieces(bottomEntry.staff.id)) {
          const y = staffBottomY(system, bottomEntry) + 45
          drawText(ctx, PEDAL_DOWN, x1, y, { size: 26, family: 'Bravura' })
          drawText(ctx, PEDAL_UP, x2 - 14, y, { size: 26, family: 'Bravura' })
        }
      } else if (spanner.kind === 'custom-spanner') {
        const staffId = spanner.staffIds?.[0] ?? firstStaffId
        for (const { system, x1, x2 } of pieces(staffId)) {
          const y = staffTopY(system, staffId) - 18
          const label = spanner.text ?? spanner.techniqueId
          drawText(ctx, label, x1, y, { size: 12, style: 'italic' })
          dashedLine(ctx, x1 + ctx.measureText(label).width + 6, x2, y - 4)
        }
      }
    }
  })

  // Tempo changes (rit. / accel.) and tempos that don't start a measure
  for (const spanner of music.globalSpanners) {
    if (spanner.kind !== 'tempo-change') continue
    const topStaff = entries[0]!.staff.id
    for (const { system, x1, x2 } of segments(topStaff, spanner.start, spanner.end, false)) {
      const y = staffTopY(system, topStaff) - 52 // above segno / coda signs
      const label = `${spanner.type}.`
      drawText(ctx, label, x1, y, { size: 13, style: 'italic' })
      dashedLine(ctx, x1 + 28, x2, y - 4)
    }
  }
  timeline.forEach(info => info.measure.tempos.filter(t => t.tickInMeasure > 0).forEach(t => {
    const tick = info.startTick + t.tickInMeasure
    const system = drawn[systemIndexAt(tick)]!
    const topStaff = entries[0]!.staff.id
    const label = t.displayText ?? `♩ = ${t.value}`
    const x = xAt(system, topStaff, tick)
    drawText(ctx, label, x, staffTopY(system, topStaff) - 30, { size: 12, weight: 'bold' })
  }))

  return { warnings }
}

/* ---------- drawing helpers ---------- */

interface TextOptions {
  size: number
  align?: 'left' | 'center' | 'right'
  weight?: 'normal' | 'bold'
  style?: 'normal' | 'italic'
  family?: string
}

function drawText(ctx: RenderContext, text: string, x: number, y: number, options: TextOptions) {
  ctx.save()
  ctx.setFont(
    options.family ?? 'Academico, Times New Roman, serif',
    options.size,
    options.weight ?? 'normal',
    options.style ?? 'normal',
  )
  const w = ctx.measureText(text).width
  const dx = options.align === 'center' ? -w / 2 : options.align === 'right' ? -w : 0
  ctx.fillText(text, x + dx, y)
  ctx.restore()
}

let measureCanvas: CanvasRenderingContext2D | null = null

/** Text width before anything is drawn, for margins. */
function textWidth(text: string, size: number): number {
  measureCanvas ??= document.createElement('canvas').getContext('2d')
  if (!measureCanvas) return text.length * size * 0.6
  measureCanvas.font = `${size}pt Academico, Times New Roman, serif` // VexFlow sizes are in pt
  return measureCanvas.measureText(text).width
}

function dashedLine(ctx: RenderContext, x1: number, x2: number, y: number) {
  if (x2 - x1 < 10) return
  ctx.save()
  ctx.setLineDash([4, 4])
  ctx.beginPath()
  ctx.moveTo(x1, y)
  ctx.lineTo(x2, y)
  ctx.stroke()
  ctx.restore()
}

function dynamicGlyphs(type: string): string {
  return [...type].map(ch => DYNAMIC_GLYPHS[ch] ?? ch).join('')
}

/** A note's x, or undefined if VexFlow hasn't positioned it (e.g. it failed to format). */
function safeX(note: VFNote): number | undefined {
  try {
    return note.getAbsoluteX()
  } catch {
    return undefined
  }
}
