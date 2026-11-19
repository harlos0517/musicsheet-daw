import type InstrumentProfile from '@/core/types/InstrumentProfile'
import type {
  ByDuration,
  NoteMatch, OutputRange, TechniqueMapping,
} from '@/core/types/InstrumentProfile'
import type { CurveEvent, MusicEvent, NoteEvent } from '@/core/types/MusicEvent'
import type { TechniqueDefinition } from '../types/PlaybackSetup'
import { clamp01, curveKey, curveSampler, evaluateCurve, shape } from './curves'
import { applyMode } from './interpretation'
import type { TempoMap } from './tempo'

export interface MidiMessage {
  timeMs: number
  type: 'note-on' | 'note-off' | 'cc'
  /** 1–16. */
  channel: number
  /** Note or controller number. */
  data1: number
  /** Velocity or controller value. */
  data2: number
  /** What produced it, for reading the output. */
  source: 'note' | 'keyswitch' | 'technique-cc' | 'curve'
}

export interface TrackRender { messages: MidiMessage[], warnings: string[] }

type Mapping = Omit<TechniqueMapping, 'matches'>

/** How long a keyswitch key is held. */
const KEYSWITCH_MS = 10
/** Ramps are checked this often (ms) to find where the CC value reaches its next step. */
const CURVE_SAMPLE_MS = 1
/** A ramp sends a new CC value once it has moved this many steps from the last one sent. */
const CC_STEP = 1
/** Minimum time between two CC messages of one ramp. Segment edges are always sent. */
const CC_MIN_GAP_MS = 10

const byDuration = (range: ByDuration, ms: number): number => {
  if (range.longMs <= range.shortMs) return ms <= range.shortMs ? range.atShort : range.atLong
  const t = Math.min(1, Math.max(0, (ms - range.shortMs) / (range.longMs - range.shortMs)))
  return range.atShort + (range.atLong - range.atShort) * t
}

const midi = (value: number, min = 0): number => Math.min(127, Math.max(min, Math.round(value)))

const mapRange = (mapping: OutputRange, value: number): number => {
  const t = mapping.curve
    ? shape(mapping.curve.curveType, mapping.curve.steepness, clamp01(value))
    : value
  return mapping.low + (mapping.high - mapping.low) * t
}

/** Same-time order: releases first, then controllers and keyswitches, then new notes. */
const ORDER: Record<string, number> = { 'note-off': 0, 'cc': 1, 'note-on': 3 }
export const messageOrder = (message: MidiMessage): number =>
  message.source === 'keyswitch' && message.type === 'note-on' ? 2 : ORDER[message.type]!

interface Sounding {
  channel: number
  pitch: number
  velocity: number
  startMs: number
  endMs: number
  keyswitch: { note: number, velocity: number } | undefined
  cc: Map<number, number>
}

/**
 * MusicEvents of one track → MIDI messages, in milliseconds.
 * `offsetMs` of the profile is left out on purpose: it belongs on the DAW track.
 */
export const renderTrack = (
  events: MusicEvent[],
  profile: InstrumentProfile,
  tempo: TempoMap,
  techniques: TechniqueDefinition[],
): TrackRender => {
  const warnings: string[] = []
  const messages: MidiMessage[] = []
  const notes = events.filter((e): e is NoteEvent => e.kind === 'note-event')
  const curves = events.filter((e): e is CurveEvent => e.kind === 'curve-event')

  const allChannels = [...new Set([
    profile.defaultChannel,
    ...profile.techniqueMappings.flatMap(mapping => mapping.channels ?? []),
  ])]

  // A technique this profile has no mapping for is replaced by its registry fallback.
  const known = new Set(profile.techniqueMappings.flatMap(mapping =>
    mapping.matches.flatMap(match => match.kind === 'technique' ? [match.techniqueId] : [])))
  const resolveTechnique = (id: string): string => {
    let current = id
    for (let guard = 0; guard < 16 && !known.has(current); guard += 1) {
      const fallback = techniques.find(t => t.id === current)?.fallback
      if (fallback === undefined) return id
      current = fallback
    }
    return current
  }

  const curveDefault = (key: string) =>
    profile.curveMappings.find(mapping => curveKey(mapping.parameter) === key)?.defaultValue ?? 0

  const test = (match: NoteMatch, note: NoteEvent, noteTechniques: string[]): boolean => {
    if (match.kind === 'technique') return noteTechniques.includes(match.techniqueId)
    if (match.kind === 'slur') return note.slur === match.position
    const value = evaluateCurve(
      curves, match.parameter, note.startTick, curveDefault(curveKey(match.parameter)),
      { staffId: note.staffId, voiceIndex: note.voiceIndex },
    )
    return match.min <= value && value <= match.max
  }

  const soundings: Sounding[] = []
  /** Turns taken so far by each mapping with several `notes`, per channel. */
  const turns = new Map<Mapping, Map<number, number>>()
  /** The same for kit sounds with several keys, by sound and channel. */
  const kitTurns = new Map<string, number>()
  for (const note of notes) {
    const pitch = typeof note.pitch === 'number'
      ? note.pitch + (profile.pitchOffset ?? 0)
      : profile.kit?.[note.pitch.sound]
    const noteTechniques = note.techniques.map(resolveTechnique)
    // A mapping with no `matches` applies to every note: that is the profile's base.
    const active: Mapping[] = profile.techniqueMappings
      .filter(mapping => mapping.matches.every(match => test(match, note, noteTechniques)))
    // A mapping may give the key itself; otherwise an unpitched note needs a kit entry.
    if (pitch === undefined && !active.some(mapping => mapping.notes?.length)) {
      warnings.push(`note ${note.id}: the profile has no kit entry for this sound`)
      continue
    }

    // First pass: which channels the note sounds on.
    let channels: number[] = []
    for (const mapping of active) {
      if (!mapping.channels?.length) continue
      channels = mapping.channelMode === 'add'
        ? [...new Set([...channels, ...mapping.channels])]
        : [...mapping.channels]
    }
    if (channels.length === 0) channels = [profile.defaultChannel]

    // Second pass: merge the mappings separately for each channel.
    const baseStart = tempo.tickToMs(note.startTick)
    const notatedMs = tempo.tickToMs(note.startTick + note.notatedTicks) - baseStart
    const baseEnd = tempo.tickToMs(note.startTick + note.durationTicks)
    for (const channel of channels) {
      let velocity = note.velocity
      let duration = baseEnd - baseStart
      let offset = note.startOffsetMs ?? 0
      let keyswitch: Sounding['keyswitch']
      let keys: { mapping: Mapping, notes: number[] } | undefined
      let alignEnd = false
      const cc = new Map<number, number>()
      for (const mapping of active) {
        if (mapping.channels && !mapping.channels.includes(channel)) continue
        if (mapping.keyswitch !== undefined) keyswitch = mapping.keyswitch
        if (mapping.notes?.length) keys = { mapping, notes: mapping.notes }
        if (mapping.alignToEnd !== undefined) alignEnd = mapping.alignToEnd
        for (const { number, value } of mapping.cc ?? [])
          cc.set(number, typeof value === 'number' ? value : byDuration(value, notatedMs))

        if (mapping.velocity)
          velocity = applyMode(mapping.velocity.mode, velocity, mapping.velocity.value)

        if (mapping.durationMs)
          duration = applyMode(mapping.durationMs.mode, duration, mapping.durationMs.value)

        offset += mapping.startOffsetMs ?? 0
      }
      // Keys played instead of the pitch take turns, counted per mapping and channel.
      let played = typeof pitch === 'number' ? pitch : 0
      if (!keys && Array.isArray(pitch) && typeof note.pitch !== 'number') {
        const turnKey = `${note.pitch.sound}:${channel}`
        const turn = kitTurns.get(turnKey) ?? 0
        kitTurns.set(turnKey, turn + 1)
        played = pitch[turn % pitch.length]!
      }
      if (keys) {
        const byChannel = turns.get(keys.mapping) ?? new Map<number, number>()
        const turn = byChannel.get(channel) ?? 0
        byChannel.set(channel, turn + 1)
        turns.set(keys.mapping, byChannel)
        played = keys.notes[turn % keys.notes.length]!
      }
      let startMs = Math.max(0, baseStart + offset)
      let endMs = Math.max(startMs + 1, baseStart + duration)
      if (alignEnd) {
        // Keeps the note's end, e.g. a crescendo sample that peaks there.
        endMs = baseEnd
        startMs = Math.max(0, endMs - duration)
      }
      soundings.push({
        channel,
        pitch: played,
        velocity: 'fixed' in profile.velocity
          ? midi(profile.velocity.fixed, 1)
          : midi(mapRange(profile.velocity.mapping, Math.min(1, Math.max(0, velocity))), 1),
        startMs,
        endMs,
        keyswitch,
        cc,
      })
    }
  }

  soundings.sort((a, b) => a.startMs - b.startMs)

  // A key cannot be pressed twice: end a note where the same key is struck again.
  const lastOnKey = new Map<string, Sounding>()
  for (const sounding of soundings) {
    const key = `${sounding.channel}:${sounding.pitch}`
    const previous = lastOnKey.get(key)
    if (previous && previous.endMs > sounding.startMs) previous.endMs = sounding.startMs
    lastOnKey.set(key, sounding)
  }

  // Keyswitches and technique CCs are sent only when they change on a channel.
  const lead = profile.keyswitchLeadMs ?? 0
  const keyswitchState = new Map<number, string>()
  const ccState = new Map<string, number>()
  const curveControllers = new Set(
    profile.curveMappings.flatMap(mapping => (mapping.cc ?? []).map(cc => cc.number)),
  )
  /** Stretches where a note's own controller value replaces the curve's. */
  const holds: { channel: number, number: number, startMs: number, endMs: number }[] = []
  const pressKeyswitch = (timeMs: number, channel: number, key: number, velocity: number) => {
    messages.push(
      { timeMs, type: 'note-on', channel, data1: key, data2: velocity, source: 'keyswitch' },
      { timeMs: timeMs + KEYSWITCH_MS, type: 'note-off', channel, data1: key, data2: 0,
        source: 'keyswitch' },
    )
  }
  for (const sounding of soundings) {
    const { channel, pitch, velocity, startMs, endMs, keyswitch, cc } = sounding
    const before = Math.max(0, startMs - lead)
    // Velocity is part of the switch: some keys select an articulation by how hard
    // they are pressed.
    const pressed = keyswitch && `${keyswitch.note}:${keyswitch.velocity}`
    if (keyswitch !== undefined && keyswitchState.get(channel) !== pressed) {
      keyswitchState.set(channel, pressed!)
      pressKeyswitch(before, channel, keyswitch.note, keyswitch.velocity)
    }
    for (const [number, value] of cc) {
      if (curveControllers.has(number)) {
        // A controller a curve also drives: take it over for as long as the note sounds.
        holds.push({ channel, number, startMs: before, endMs })
        messages.push({
          timeMs: before, type: 'cc', channel, data1: number, data2: midi(value),
          source: 'technique-cc',
        })
        continue
      }
      if (ccState.get(`${channel}:${number}`) === value) continue
      ccState.set(`${channel}:${number}`, value)
      messages.push({
        timeMs: before, type: 'cc', channel, data1: number, data2: midi(value),
        source: 'technique-cc',
      })
    }
    messages.push(
      { timeMs: startMs, type: 'note-on', channel, data1: pitch, data2: velocity, source: 'note' },
      { timeMs: endMs, type: 'note-off', channel, data1: pitch, data2: 0, source: 'note' },
    )
  }

  // A hold lasts until the next note on its channel, so that the curve does not come back
  // in the silence between two short notes. That note sends its own value or resumes the
  // curve, ahead of its keyswitch lead.
  const startsOn = new Map<number, number[]>()
  for (const sounding of soundings) {
    const list = startsOn.get(sounding.channel) ?? []
    list.push(Math.max(0, sounding.startMs - lead))
    startsOn.set(sounding.channel, list)
  }
  for (const hold of holds) {
    // Starts are sorted: the first one after the hold's own, by binary search.
    const starts = startsOn.get(hold.channel)!
    let low = 0
    let high = starts.length
    while (low < high) {
      const middle = (low + high) >> 1
      if (starts[middle]! > hold.startMs) high = middle
      else low = middle + 1
    }
    if (low < starts.length) hold.endMs = Math.max(hold.endMs, starts[low]!)
  }

  // A damp key, held from where the sound should stop until the next note comes or a
  // beat has passed, whichever is first; letting go switches the patch back. It stops
  // the sound at rests (not after a note left to ring) and right after notes that ask
  // for it, such as staccato ones.
  if (profile.dampKeyswitch) {
    const { note: key, velocity: keyVelocity, atRests = true } = profile.dampKeyswitch
    const after = new Set(profile.dampKeyswitch.afterTechniques ?? [])
    const channel = profile.defaultChannel
    const spans = notes.map(note => ({
      startMs: tempo.tickToMs(note.startTick),
      endMs: tempo.tickToMs(note.startTick + note.notatedTicks),
      playedEndMs: tempo.tickToMs(note.startTick + note.durationTicks),
      rings: note.techniques.includes('core:let-ring'),
      cut: note.techniques.some(id => after.has(id)),
    })).sort((a, b) => a.startMs - b.startMs)
    /** A beat (quarter note) at the tempo in force at a time. */
    const beatMs = (ms: number) => {
      const tick = tempo.msToTick(ms)
      return 60000 / (tempo.points.findLast(point => point.tick <= tick)?.qpm ?? 120)
    }
    const nextStart = (ms: number) => spans.find(span => span.startMs >= ms)?.startMs
    const windows: { from: number, to: number }[] = []
    const damp = (from: number) => {
      const next = nextStart(from)
      const to = Math.min(from + beatMs(from), next === undefined ? Infinity : next - lead)
      if (to > from) windows.push({ from, to })
    }
    let end = -Infinity
    let rings = false
    spans.forEach((span, i) => {
      if (span.cut && !spans.some(other => other !== span
        && other.startMs < span.playedEndMs + 1 && other.startMs > span.startMs))
        damp(span.playedEndMs)

      if (span.endMs >= end) {
        end = span.endMs
        rings = span.rings
      }
      const next = spans[i + 1]
      if (atRests && !rings && (!next || next.startMs > end + 1)) damp(end)
    })
    // One press for each stretch of silence, however many reasons it has.
    windows.sort((a, b) => a.from - b.from)
    const merged: typeof windows = []
    for (const window of windows) {
      const last = merged.at(-1)
      if (last && window.from <= last.to) last.to = Math.max(last.to, window.to)
      else merged.push({ ...window })
    }
    const source = 'keyswitch'
    for (const { from, to } of merged) {
      messages.push(
        { timeMs: from, type: 'note-on', channel, data1: key, data2: keyVelocity, source },
        { timeMs: to, type: 'note-off', channel, data1: key, data2: 0, source },
      )
    }
  }

  // Curves → controllers and range keyswitches.
  for (const mapping of profile.curveMappings) {
    const key = curveKey(mapping.parameter)
    const own = curves.filter(curve => curveKey(curve.parameter) === key)
    // Tick → whether it must be sent (a segment edge) rather than being a ramp sample.
    const ticks = new Map<number, boolean>([[0, true]])
    for (const curve of own) {
      const end = curve.startTick + curve.durationTicks
      if (curve.durationTicks > 0 && curve.curveType !== 'hold') {
        const endMs = tempo.tickToMs(end)
        for (let ms = tempo.tickToMs(curve.startTick) + CURVE_SAMPLE_MS; ms < endMs;
          ms += CURVE_SAMPLE_MS) {
          const tick = tempo.msToTick(ms)
          if (!ticks.has(tick)) ticks.set(tick, false)
        }
      }
      ticks.set(curve.startTick, true)
      ticks.set(end, true)
    }
    const channels = mapping.channels ?? allChannels
    const sorted = [...ticks.keys()].sort((a, b) => a - b)
    // A controller is per channel, so segments limited to some voices are not kept apart.
    const sampler = () => curveSampler(own, mapping.parameter, mapping.defaultValue)

    for (const channel of channels) {
      for (const { number, mapping: range } of mapping.cc ?? []) {
        const taken = holds
          .filter(hold => hold.channel === channel && hold.number === number)
          .sort((a, b) => a.startMs - b.startMs)
        // The curve's own points, plus one where each hold ends to pick the curve up again.
        const points = [
          ...sorted.map(tick => ({
            timeMs: Math.max(0, tempo.tickToMs(tick)), tick, resume: false, edge: ticks.get(tick)!,
          })),
          ...taken.map(hold => ({
            timeMs: hold.endMs, tick: tempo.msToTick(hold.endMs), resume: true, edge: true,
          })),
        ].sort((a, b) => a.timeMs - b.timeMs)
        let last: number | undefined
        let lastMs = -Infinity
        let h = 0
        const valueAt = sampler()
        for (const point of points) {
          while (h < taken.length && taken[h]!.endMs <= point.timeMs) h += 1
          const held = h < taken.length && taken[h]!.startMs <= point.timeMs
          if (held) continue
          const data2 = midi(mapRange(range, valueAt(point.tick)))
          if (!point.resume && last === data2) continue
          const small = last !== undefined && Math.abs(data2 - last) < CC_STEP
          const early = point.timeMs - lastMs < CC_MIN_GAP_MS
          if (!point.edge && (small || early)) continue
          last = data2
          lastMs = point.timeMs
          messages.push({
            timeMs: point.timeMs, type: 'cc', channel, data1: number, data2, source: 'curve',
          })
        }
      }
      let pressed: number | undefined
      const valueAt = sampler()
      for (const tick of mapping.keyswitch ? sorted : []) {
        const value = valueAt(tick)
        const range = mapping.keyswitch?.findLast(r => r.min <= value && value <= r.max)
        if (!range || pressed === range.note) continue
        pressed = range.note
        pressKeyswitch(Math.max(0, tempo.tickToMs(tick)), channel, range.note, range.velocity)
      }
    }
  }

  messages.sort((a, b) => a.timeMs - b.timeMs || messageOrder(a) - messageOrder(b))
  return { messages, warnings }
}
