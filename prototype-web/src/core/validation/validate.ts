import type MusicData from '@/core/types/MusicData'
import type {
  Chord,
  Duration,
  ElementAnchor,
  MusicDataNode,
  Note,
  Staff,
  StaffMeasure,
  TickAnchor,
  Track,
  UnpitchedNote,
  VoiceEvent,
} from '@/core/types/MusicData'
import { ClefType, NoteType } from '@/core/types/MusicData'
import { midiNumber } from './pitch'
import {
  type MeasureInfo,
  durationTicks,
  measureTimeline,
  timeVoice,
  voiceTicks,
} from './time'

export type Severity = 'error' | 'warning'

export interface Problem {
  severity: Severity
  code: string
  message: string
  /** Where in the data, e.g. `tracks[0].staves[1].measures[3].voices[0].events[2]`. */
  path: string
  id?: string
}

/** Where an element sits; built once so references can be checked. */
interface Located {
  node: MusicDataNode
  path: string
  trackId?: string
  staffId?: string
  /** Absolute start tick (written order). */
  tick?: number
  /** Real length in ticks, after tuplet scaling. */
  ticks?: number
  isGrace?: boolean
}

type Where = Omit<Located, 'node' | 'path'>

type Anchor = ElementAnchor | TickAnchor

const NOTE_TYPES = new Set<number>(
  Object.values(NoteType).filter((v): v is number => typeof v === 'number'),
)
const ANCHORABLE = new Set(['chord', 'rest', 'note', 'unpitched-note'])
const TECHNIQUE_ID = /^[a-z0-9-]+:[a-z0-9-]+$/i

const isInt = (n: number) => Number.isInteger(n)
const isPowerOfTwo = (n: number) => isInt(n) && n > 0 && (n & (n - 1)) === 0

/**
 * Checks everything the types alone can't: timing, references between elements,
 * alignment of staves with the measure list, and unique IDs.
 * Returns an empty list for a valid score.
 */
export const validate = (music: MusicData): Problem[] => {
  const problems: Problem[] = []
  const index = new Map<string, Located>()
  const report = (
    severity: Severity, code: string, path: string, message: string, id?: string,
  ) => problems.push({ severity, code, path, message, ...(id !== undefined ? { id } : {}) })
  const error = (code: string, path: string, message: string, id?: string) =>
    report('error', code, path, message, id)
  const warn = (code: string, path: string, message: string, id?: string) =>
    report('warning', code, path, message, id)

  const register = (node: MusicDataNode, path: string, extra: Where = {}) => {
    if (typeof node.id !== 'string' || node.id === '') {
      error('missing-id', path, `${node.kind} has no id`)
      return
    }
    const existing = index.get(node.id)
    if (existing) {
      error('duplicate-id', path, `id "${node.id}" is also used at ${existing.path}`, node.id)
      return
    }
    index.set(node.id, { node, path, ...extra })
  }

  const { tpq } = music
  if (!isInt(tpq) || tpq <= 0)
    error('invalid-tpq', 'tpq', `tpq must be a positive integer, got ${tpq}`)

  /* ---------- measures ---------- */

  const first = music.measures[0]
  if (first && !first.timeSig)
    error('missing-initial-time-sig', 'measures[0]', 'the first measure needs a time signature')
  if (first && !first.key)
    error('missing-initial-key', 'measures[0]', 'the first measure needs a key signature')

  const timeline = measureTimeline(music)
  const measureIndexById = new Map<string, number>()

  timeline.forEach(({ measure, lengthTicks }, i) => {
    const path = `measures[${i}]`
    register(measure, path)
    measureIndexById.set(measure.id, i)

    if (measure.timeSig) {
      const sPath = `${path}.timeSig`
      register(measure.timeSig, sPath)
      const { beats, unit } = measure.timeSig
      if (!isInt(beats) || beats <= 0)
        error('invalid-time-sig', sPath, `beats must be a positive integer, got ${beats}`)
      if (!isPowerOfTwo(unit) || unit > 64)
        error('invalid-time-sig', sPath, `unit must be 1, 2, 4 … 64, got ${unit}`)
    }
    if (measure.key) {
      register(measure.key, `${path}.key`)
      const { accidentals } = measure.key
      if (!isInt(accidentals) || accidentals < -7 || accidentals > 7)
        error('invalid-key', `${path}.key`, `accidentals must be -7…7, got ${accidentals}`)
    }
    const { actualTicks } = measure
    if (actualTicks !== undefined && (!isInt(actualTicks) || actualTicks <= 0)) {
      error(
        'invalid-actual-ticks', `${path}.actualTicks`,
        `actualTicks must be a positive integer, got ${actualTicks}`,
      )
    }
    if (!isInt(lengthTicks))
      error('non-integer-ticks', path, `measure length is ${lengthTicks} ticks; raise tpq`)

    measure.tempos.forEach((tempo, j) => {
      const tPath = `${path}.tempos[${j}]`
      register(tempo, tPath)
      checkDuration(tempo.unit, `${tPath}.unit`)
      if (!(tempo.value > 0))
        error('invalid-tempo', tPath, `tempo value must be positive, got ${tempo.value}`)
      if (tempo.tickInMeasure < 0 || tempo.tickInMeasure >= lengthTicks) {
        error(
          'tick-out-of-range', tPath,
          `tickInMeasure ${tempo.tickInMeasure} is outside the measure (0…${lengthTicks - 1})`,
        )
      }
    })
    const { repeatEnd } = measure
    if (repeatEnd && (!isInt(repeatEnd.times) || repeatEnd.times < 2)) {
      error(
        'invalid-repeat', `${path}.repeatEnd`,
        `repeat times must be an integer ≥ 2, got ${repeatEnd.times}`,
      )
    }
    measure.markers.forEach((marker, j) =>
      register(marker, `${path}.markers[${j}]`))
    measure.jumps.forEach((jump, j) =>
      register(jump, `${path}.jumps[${j}]`))
  })

  function checkDuration(duration: Duration, path: string) {
    if (!NOTE_TYPES.has(duration.type))
      error('invalid-duration', path, `unknown note type ${duration.type}`)
    if (!isInt(duration.dot) || duration.dot < 0 || duration.dot > 4)
      error('invalid-duration', path, `dots must be an integer 0…4, got ${duration.dot}`)
    const ticks = durationTicks(duration, tpq)
    if (!isInt(ticks)) error('non-integer-ticks', path, `duration is ${ticks} ticks; raise tpq`)
  }

  /* ---------- tracks ---------- */

  const tieSources: { note: Note | UnpitchedNote, path: string }[] = []

  music.tracks.forEach((track, t) => {
    const tPath = `tracks[${t}]`
    register(track, tPath, { trackId: track.id })

    if (track.staves.length === 0)
      error('no-staves', tPath, `track "${track.instrumentName}" has no staves`)
    if (track.transposition) {
      const { diatonic, chromatic, octave } = track.transposition
      if (![diatonic, chromatic, octave].every(isInt)) {
        error(
          'invalid-transposition', `${tPath}.transposition`,
          'transposition values must be integers',
        )
      }
    }
    track.kit?.forEach((entry, k) => {
      const kPath = `${tPath}.kit[${k}]`
      register(entry, kPath, { trackId: track.id })
      if (!isInt(entry.displayPitch))
        error('invalid-kit-entry', kPath, 'displayPitch must be an integer')
    })

    track.staves.forEach((staff, s) => checkStaff(track, staff, `${tPath}.staves[${s}]`))
  })

  function checkStaff(track: Track, staff: Staff, path: string) {
    register(staff, path, { trackId: track.id, staffId: staff.id })
    if (staff.lines !== undefined && (!isInt(staff.lines) || staff.lines < 1)) {
      error(
        'invalid-staff-lines', `${path}.lines`,
        `lines must be a positive integer, got ${staff.lines}`,
      )
    }

    if (staff.measures.length !== timeline.length) {
      error(
        'staff-measure-count', path,
        `staff has ${staff.measures.length} measures, the score has ${timeline.length}`,
      )
    }

    staff.measures.forEach((staffMeasure, m) => {
      const info = timeline[m]
      const mPath = `${path}.measures[${m}]`
      if (info && staffMeasure.measureId !== info.measure.id) {
        error(
          'staff-measure-mismatch', mPath,
          `measureId "${staffMeasure.measureId}" should be "${info.measure.id}"`,
        )
      }
      register(staffMeasure, mPath, { trackId: track.id, staffId: staff.id })
      if (info) checkStaffMeasure(track, staff, staffMeasure, info, mPath, m === 0)
    })
  }

  function checkStaffMeasure(
    track: Track,
    staff: Staff,
    staffMeasure: StaffMeasure,
    info: MeasureInfo,
    path: string,
    isFirst: boolean,
  ) {
    const { lengthTicks, startTick } = info

    let lastClefTick = -1
    staffMeasure.clefs.forEach(({ tick, clef }, c) => {
      const cPath = `${path}.clefs[${c}]`
      register(clef, `${cPath}.clef`)
      if (!isInt(tick) || tick < 0 || tick > lengthTicks) {
        error(
          'tick-out-of-range', cPath,
          `clef tick ${tick} is outside the measure (0…${lengthTicks})`,
        )
      }
      if (tick <= lastClefTick)
        error('clef-order', cPath, 'clefs must be in increasing tick order, one per tick')
      lastClefTick = tick
      if (clef.type === ClefType.PERCUSSION && !track.kit)
        warn('percussion-without-kit', cPath, 'percussion clef on a track without a kit')
    })
    if (isFirst && staffMeasure.clefs[0]?.tick !== 0)
      error('missing-initial-clef', path, 'the first measure of each staff needs a clef at tick 0')

    if (staffMeasure.voices.length === 0) error('no-voices', path, 'measure has no voices')

    const voiceIndexes = new Set<number>()
    staffMeasure.voices.forEach((voice, v) => {
      const vPath = `${path}.voices[${v}]`
      register(voice, vPath, { trackId: track.id, staffId: staff.id })

      if (!isInt(voice.index) || voice.index < 0) {
        error(
          'invalid-voice-index', vPath,
          `voice index must be an integer ≥ 0, got ${voice.index}`,
        )
      } else if (voiceIndexes.has(voice.index)) {
        error(
          'duplicate-voice-index', vPath,
          `voice index ${voice.index} is used twice in this measure`,
        )
      }
      voiceIndexes.add(voice.index)

      const length = voiceTicks(voice.events, tpq)
      if (length !== lengthTicks) {
        error(
          'voice-length', vPath,
          `voice lasts ${length} ticks, the measure lasts ${lengthTicks}`,
        )
      }

      const firstEvent = voice.events[0]
      const joinsNothing = firstEvent
        && firstEvent.kind !== 'tuplet'
        && firstEvent.kind !== 'space'
        && firstEvent.beam === 'join-previous'
      if (joinsNothing) {
        warn(
          'beam-join-first', `${vPath}.events[0]`,
          'the first event of a voice has nothing to join',
        )
      }

      const timed = timeVoice(voice.events, tpq, startTick)
      const paths = new Map<VoiceEvent, string>()
      const walkPaths = (events: VoiceEvent[], prefix: string) => events.forEach((e, i) => {
        paths.set(e, `${prefix}[${i}]`)
        if (e.kind === 'tuplet') walkPaths(e.events, `${prefix}[${i}].events`)
      })
      walkPaths(voice.events, `${vPath}.events`)

      for (const { event, tick, ticks, tuplets } of timed) {
        const ePath = paths.get(event)!
        const where: Where = {
          trackId: track.id, staffId: staff.id, tick, ticks,
        }
        if (!isInt(tick) || !isInt(ticks))
          error('non-integer-ticks', ePath, `event at tick ${tick} lasts ${ticks}; raise tpq`)

        switch (event.kind) {
          case 'tuplet': {
            register(event, ePath, where)
            checkDuration(event.unit, `${ePath}.unit`)
            const { actual, normal } = event
            if (!isInt(actual) || actual <= 0 || !isInt(normal) || normal <= 0) {
              error('invalid-tuplet', ePath, 'actual and normal must be positive integers')
              break
            }
            if (event.events.length === 0) error('invalid-tuplet', ePath, 'tuplet is empty')
            const content = voiceTicks(event.events, tpq)
            const expected = durationTicks(event.unit, tpq) * actual
            if (content !== expected) {
              error(
                'tuplet-length', ePath,
                `tuplet contents last ${content} ticks, expected ${expected} (${actual} × unit)`,
              )
            }
            break
          }
          case 'space':
            register(event, ePath, where)
            checkDuration(event.duration, `${ePath}.duration`)
            break
          case 'rest':
            register(event, ePath, where)
            checkDuration(event.duration, `${ePath}.duration`)
            if (event.fullMeasure && (voice.events.length !== 1 || tuplets.length > 0)) {
              error(
                'full-measure-rest', ePath,
                'a full-measure rest must be the only event in its voice',
              )
            }
            break
          case 'chord':
            register(event, ePath, where)
            checkChord(track, event, ePath, where, false)
            if (event.graceNotes) checkGrace(track, event, ePath, where)
            break
        }
      }
    })
  }

  function checkGrace(track: Track, chord: Chord, path: string, where: Where) {
    const grace = chord.graceNotes!
    const gPath = `${path}.graceNotes`
    register(grace, gPath, where)
    if (grace.chords.length === 0) warn('empty-grace', gPath, 'grace group has no chords')
    grace.chords.forEach((g, i) => {
      register(g, `${gPath}.chords[${i}]`, { ...where, isGrace: true })
      checkChord(track, g, `${gPath}.chords[${i}]`, { ...where, isGrace: true }, true)
    })
  }

  function checkChord(
    track: Track,
    chord: Omit<Chord, 'graceNotes'>,
    path: string,
    where: Where,
    isGrace: boolean,
  ) {
    checkDuration(chord.duration, `${path}.duration`)
    if (chord.notes.length === 0) error('empty-chord', path, 'chord has no notes; use a rest')

    const seen = new Set<string>()
    chord.notes.forEach((note, n) => {
      const nPath = `${path}.notes[${n}]`
      register(note, nPath, { ...where, isGrace })
      let key: string
      if (note.kind === 'note') {
        const { step, alter, octave } = note.pitch
        if (!isInt(alter) || alter < -2 || alter > 2)
          error('invalid-pitch', `${nPath}.pitch`, `alter must be -2…2, got ${alter}`)
        if (!isInt(octave) || octave < 0 || octave > 9)
          error('invalid-pitch', `${nPath}.pitch`, `octave must be 0…9, got ${octave}`)
        key = `${step}${alter}/${octave}`
      } else {
        if (!track.kit)
          error('unpitched-without-kit', nPath, 'unpitched note on a track without a kit')
        else if (!track.kit.some(k => k.id === note.kitEntryId)) {
          error(
            'unknown-kit-entry', nPath,
            `kit entry "${note.kitEntryId}" does not exist on this track`,
          )
        }
        key = `kit:${note.kitEntryId}`
      }
      if (seen.has(key)) warn('duplicate-note', nPath, 'the same note appears twice in this chord')
      seen.add(key)
      if (note.tie && 'to' in note.tie) tieSources.push({ note, path: nPath })
    })

    const types = new Set<string>()
    chord.articulations.forEach((a, i) => {
      const aPath = `${path}.articulations[${i}]`
      register(a, aPath)
      if (types.has(a.type)) warn('duplicate-articulation', aPath, `${a.type} appears twice`)
      types.add(a.type)
    })
  }

  /* ---------- references ---------- */

  // Ties: the target must be the same pitch, on the same track,
  // starting exactly where the source ends.
  const tieTargets = new Set<string>()
  for (const { note, path } of tieSources) {
    const tiePath = `${path}.tie`
    const to = (note.tie as { to: string }).to
    const source = index.get(note.id)
    const target = index.get(to)
    if (!target) {
      error('tie-target-missing', tiePath, `tie target "${to}" does not exist`, note.id)
      continue
    }
    const targetNode = target.node
    if (targetNode.kind !== note.kind) {
      error(
        'tie-target-kind', tiePath,
        `tie target "${to}" is a ${targetNode.kind}, expected a ${note.kind}`, note.id,
      )
      continue
    }
    if (tieTargets.has(to))
      warn('tie-duplicate-target', tiePath, `more than one note ties into "${to}"`, note.id)
    tieTargets.add(to)
    if (source?.trackId !== target.trackId)
      error('tie-cross-track', tiePath, 'ties must stay within one track', note.id)

    if (note.kind === 'note' && targetNode.kind === 'note') {
      const targetPitch = (targetNode as Note).pitch
      if (midiNumber(note.pitch) !== midiNumber(targetPitch)) {
        error(
          'tie-pitch-mismatch', tiePath,
          `tied notes have different pitches (target at ${target.path})`, note.id,
        )
      } else if (note.pitch.step !== targetPitch.step)
        warn('tie-enharmonic', tiePath, 'tied notes are spelled differently', note.id)
    } else if (
      note.kind === 'unpitched-note'
      && (targetNode as UnpitchedNote).kitEntryId !== note.kitEntryId
    ) {
      error(
        'tie-pitch-mismatch', tiePath,
        'tied unpitched notes use different kit entries', note.id,
      )
    }

    const sourceEnd = source?.tick !== undefined && source.ticks !== undefined
      ? source.tick + source.ticks
      : undefined
    const timed = !source?.isGrace && !target.isGrace && sourceEnd !== undefined
    if (timed && target.tick !== sourceEnd) {
      error(
        'tie-not-adjacent', tiePath,
        `tie target starts at tick ${target.tick}, but the note ends at ${sourceEnd}`, note.id,
      )
    }
  }

  /** Resolves an anchor to an absolute tick, reporting problems. Returns undefined if it can't. */
  const resolveAnchor = (
    anchor: Anchor, path: string, trackId: string | undefined, staffId?: string,
  ): number | undefined => {
    if (anchor.type === 'element') {
      const found = index.get(anchor.id)
      if (!found) {
        error('anchor-missing', path, `element "${anchor.id}" does not exist`)
        return undefined
      }
      if (!ANCHORABLE.has(found.node.kind)) {
        error(
          'anchor-kind', path,
          `"${anchor.id}" is a ${found.node.kind}; anchors must point at a chord, rest or note`,
        )
        return undefined
      }
      if (trackId !== undefined && found.trackId !== trackId)
        error('anchor-wrong-track', path, `"${anchor.id}" belongs to another track`)
      if (staffId !== undefined && found.staffId !== staffId)
        error('anchor-wrong-staff', path, `"${anchor.id}" belongs to another staff`)
      return found.tick
    }
    const m = measureIndexById.get(anchor.measureId)
    if (m === undefined) {
      error('anchor-missing', path, `measure "${anchor.measureId}" does not exist`)
      return undefined
    }
    const info = timeline[m]!
    if (!isInt(anchor.tick) || anchor.tick < 0 || anchor.tick > info.lengthTicks) {
      error(
        'tick-out-of-range', path,
        `tick ${anchor.tick} is outside the measure (0…${info.lengthTicks})`,
      )
      return undefined
    }
    return info.startTick + anchor.tick
  }

  const checkRange = (
    start: Anchor, end: Anchor, path: string, trackId: string | undefined, staffId?: string,
  ) => {
    const a = resolveAnchor(start, `${path}.start`, trackId, staffId)
    const b = resolveAnchor(end, `${path}.end`, trackId, staffId)
    if (a !== undefined && b !== undefined && b < a)
      error('spanner-reversed', path, `ends (tick ${b}) before it starts (tick ${a})`)
  }

  const checkPasses = (passes: number[] | undefined, path: string) => {
    if (!passes) return
    if (passes.length === 0)
      warn('empty-passes', path, 'passes is empty, so this never applies')
    if (passes.some(p => !isInt(p) || p < 1))
      error('invalid-passes', path, 'passes must be integers ≥ 1')
    if (new Set(passes).size !== passes.length)
      warn('duplicate-passes', path, 'passes contains duplicates')
  }

  const checkTechniqueId = (techniqueId: string, path: string) => {
    if (!TECHNIQUE_ID.test(techniqueId)) {
      warn(
        'technique-id-format', path,
        `technique id "${techniqueId}" should look like "namespace:name"`,
      )
    }
  }

  music.tracks.forEach((track, t) => {
    const tPath = `tracks[${t}]`
    const staffIds = new Set(track.staves.map(s => s.id))
    /** Voice indexes used anywhere on the given staves (default: all staves of the track). */
    const voiceIndexesOn = (ids: string[] | undefined) => new Set(
      track.staves
        .filter(s => !ids || ids.includes(s.id))
        .flatMap(s => s.measures.flatMap(m => m.voices.map(v => v.index))),
    )
    const checkStaffIds = (ids: string[] | undefined, path: string) => ids?.forEach(id => {
      if (!staffIds.has(id)) error('unknown-staff', path, `staff "${id}" is not part of this track`)
    })
    const checkVoiceIndexes = (
      indexes: number[] | undefined, onStaffIds: string[] | undefined, path: string,
    ) => {
      if (!indexes) return
      const known = voiceIndexesOn(onStaffIds)
      for (const index of indexes)
        if (!known.has(index)) error('unknown-voice', path, `no voice has index ${index} here`)
    }

    track.trackSpanners.forEach((spanner, i) => {
      const path = `${tPath}.trackSpanners[${i}]`
      register(spanner, path, { trackId: track.id })
      checkRange(spanner.start, spanner.end, path, track.id)
      switch (spanner.kind) {
        case 'slur': {
          const onRest = [spanner.start, spanner.end]
            .some(anchor => index.get(anchor.id)?.node.kind === 'rest')
          if (onRest) warn('slur-on-rest', path, 'slur starts or ends on a rest')
          break
        }
        case 'hairpin':
          checkStaffIds(spanner.staffIds, path)
          break
        case 'custom-spanner':
          checkStaffIds(spanner.staffIds, path)
          checkVoiceIndexes(spanner.voiceIndexes, spanner.staffIds, path)
          checkPasses(spanner.passes, `${path}.passes`)
          checkTechniqueId(spanner.techniqueId, `${path}.techniqueId`)
          break
        case 'pedal':
          break
      }
    })

    track.staves.forEach((staff, s) => staff.staffSpanners.forEach((spanner, i) => {
      const path = `${tPath}.staves[${s}].staffSpanners[${i}]`
      register(spanner, path, { trackId: track.id, staffId: staff.id })
      checkRange(spanner.start, spanner.end, path, track.id, staff.id)
    }))

    track.attachments.forEach((attachment, i) => {
      const path = `${tPath}.attachments[${i}]`
      register(attachment, path, { trackId: track.id })
      resolveAnchor(attachment.at, `${path}.at`, track.id)
      checkStaffIds(attachment.staffIds, path)
      checkVoiceIndexes(attachment.voiceIndex, attachment.staffIds, path)
      if (attachment.kind === 'technique') {
        checkPasses(attachment.passes, `${path}.passes`)
        checkTechniqueId(attachment.techniqueId, `${path}.techniqueId`)
      }
    })
  })

  music.globalSpanners.forEach((spanner, i) => {
    const path = `globalSpanners[${i}]`
    register(spanner, path)
    if (spanner.kind === 'tempo-change') {
      checkRange(spanner.start, spanner.end, path, undefined)
      if (spanner.targetTempo !== undefined && !(spanner.targetTempo > 0))
        error('invalid-tempo', path, `targetTempo must be positive, got ${spanner.targetTempo}`)
      return
    }
    const { startMeasureId, endMeasureId } = spanner
    const a = measureIndexById.get(startMeasureId)
    const b = measureIndexById.get(endMeasureId)
    const missing = (id: string) => `measure "${id}" does not exist`
    if (a === undefined) error('anchor-missing', `${path}.startMeasureId`, missing(startMeasureId))
    if (b === undefined) error('anchor-missing', `${path}.endMeasureId`, missing(endMeasureId))
    if (a !== undefined && b !== undefined && b < a)
      error('spanner-reversed', path, 'volta ends before it starts')
    if (spanner.passes.length === 0)
      error('invalid-passes', `${path}.passes`, 'a volta needs at least one pass')
    else checkPasses(spanner.passes, `${path}.passes`)
  })

  // Jumps must point at markers of the right type.
  music.measures.forEach((measure, m) => measure.jumps.forEach((jump, j) => {
    const path = `measures[${m}].jumps[${j}]`
    const checkMarker = (id: string, field: string, allowed: string[]) => {
      const fPath = `${path}.${field}`
      const found = index.get(id)
      if (!found) {
        error('jump-target', fPath, `marker "${id}" does not exist`)
        return
      }
      if (found.node.kind !== 'marker') {
        error('jump-target', fPath, `"${id}" is a ${found.node.kind}, not a marker`)
        return
      }
      const type = (found.node as unknown as { type: string }).type
      if (!allowed.includes(type)) {
        error(
          'jump-target', fPath,
          `"${id}" is a ${type} marker; expected ${allowed.join(' or ')}`,
        )
      }
    }
    if (jump.to.type === 'marker') checkMarker(jump.to.id, 'to', ['segno', 'coda'])
    if (jump.until !== undefined) checkMarker(jump.until, 'until', ['fine', 'to-coda'])
    if (jump.then !== undefined) checkMarker(jump.then, 'then', ['coda'])
  }))

  return problems
}
