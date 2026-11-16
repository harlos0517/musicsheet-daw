import type InstrumentProfile from '@/core/types/InstrumentProfile'
import type {
    NoteMatch, OutputRange, TechniqueMapping,
} from '@/core/types/InstrumentProfile'
import type { CurveEvent, MusicEvent, NoteEvent } from '@/core/types/MusicEvent'
import type { TechniqueDefinition } from '../types/PlaybackSetup'
import { curveKey, evaluateCurve } from './curves'
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
/** Curves are sampled this many times per quarter note when turned into CC values. */
const CURVE_STEPS_PER_QUARTER = 16

const midi = (value: number, min = 0): number => Math.min(127, Math.max(min, Math.round(value)))

const mapRange = (mapping: OutputRange, value: number): number =>
  mapping.low + (mapping.high - mapping.low) * value

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
  tpq: number,
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
  for (const note of notes) {
    const pitch = typeof note.pitch === 'number' ? note.pitch : profile.kit?.[note.pitch.sound]
    if (pitch === undefined) {
      warnings.push(`note ${note.id}: the profile has no kit entry for this sound`)
      continue
    }
    const noteTechniques = note.techniques.map(resolveTechnique)
    // A mapping with no `matches` applies to every note: that is the profile's base.
    const active: Mapping[] = profile.techniqueMappings
      .filter(mapping => mapping.matches.every(match => test(match, note, noteTechniques)))

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
    const baseEnd = tempo.tickToMs(note.startTick + note.durationTicks)
    for (const channel of channels) {
      let velocity = note.velocity
      let duration = baseEnd - baseStart
      let offset = note.startOffsetMs ?? 0
      let keyswitch: Sounding['keyswitch']
      const cc = new Map<number, number>()
      for (const mapping of active) {
        if (mapping.channels && !mapping.channels.includes(channel)) continue
        if (mapping.keyswitch !== undefined) keyswitch = mapping.keyswitch
        for (const { number, value } of mapping.cc ?? []) cc.set(number, value)
        if (mapping.velocity)
          velocity = applyMode(mapping.velocity.mode, velocity, mapping.velocity.value)

        if (mapping.durationMs)
          duration = applyMode(mapping.durationMs.mode, duration, mapping.durationMs.value)

        offset += mapping.startOffsetMs ?? 0
      }
      const startMs = Math.max(0, baseStart + offset)
      soundings.push({
        channel,
        pitch,
        velocity: 'fixed' in profile.velocity
          ? midi(profile.velocity.fixed, 1)
          : midi(mapRange(profile.velocity.mapping, Math.min(1, Math.max(0, velocity))), 1),
        startMs,
        endMs: Math.max(startMs + 1, baseStart + duration),
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
  const keyswitchState = new Map<number, number>()
  const ccState = new Map<string, number>()
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
    if (keyswitch !== undefined && keyswitchState.get(channel) !== keyswitch.note) {
      keyswitchState.set(channel, keyswitch.note)
      pressKeyswitch(before, channel, keyswitch.note, keyswitch.velocity)
    }
    for (const [number, value] of cc) {
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

  // Curves → controllers and range keyswitches.
  for (const mapping of profile.curveMappings) {
    const key = curveKey(mapping.parameter)
    const own = curves.filter(curve => curveKey(curve.parameter) === key)
    const ticks = new Set<number>([0])
    for (const curve of own) {
      const end = curve.startTick + curve.durationTicks
      const stepped = curve.durationTicks > 0 && curve.curveType !== 'hold'
      const step = tpq / CURVE_STEPS_PER_QUARTER
      for (let tick = curve.startTick; stepped && tick < end; tick += step) ticks.add(tick)
      ticks.add(curve.startTick)
      ticks.add(end)
    }
    const channels = mapping.channels ?? allChannels
    const sent = new Map<string, number>()
    for (const tick of [...ticks].sort((a, b) => a - b)) {
      // A controller is per channel, so segments limited to some voices are not kept apart.
      const value = evaluateCurve(own, mapping.parameter, tick, mapping.defaultValue)
      const timeMs = Math.max(0, tempo.tickToMs(tick))
      for (const channel of channels) {
        for (const { number, mapping: range } of mapping.cc ?? []) {
          const data2 = midi(mapRange(range, value))
          if (sent.get(`cc:${channel}:${number}`) === data2) continue
          sent.set(`cc:${channel}:${number}`, data2)
          messages.push({ timeMs, type: 'cc', channel, data1: number, data2, source: 'curve' })
        }
        const range = mapping.keyswitch?.findLast(r => r.min <= value && value <= r.max)
        if (range && sent.get(`ks:${channel}`) !== range.note) {
          sent.set(`ks:${channel}`, range.note)
          pressKeyswitch(timeMs, channel, range.note, range.velocity)
        }
      }
    }
  }

  messages.sort((a, b) => a.timeMs - b.timeMs || messageOrder(a) - messageOrder(b))
  return { messages, warnings }
}
