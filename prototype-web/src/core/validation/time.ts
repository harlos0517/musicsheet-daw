import type MusicData from '@/core/types/MusicData'
import type {
  Duration,
  KeySignature,
  Measure,
  TimeSignature,
  Tuplet,
  VoiceEvent,
} from '@/core/types/MusicData'

/** Nominal length of a duration in ticks, ignoring any enclosing tuplet. */
export const durationTicks = (duration: Duration, tpq: number): number =>
  tpq * 2 ** duration.type * (2 - 2 ** -duration.dots)

/** How many ticks a tuplet occupies in its parent: `normal` units of `unit`. */
const tupletTicks = (tuplet: Tuplet, tpq: number): number =>
  durationTicks(tuplet.unit, tpq) * tuplet.normal

/** Ticks an event occupies in its parent voice (or parent tuplet, before scaling). */
const eventTicks = (event: VoiceEvent, tpq: number): number =>
  event.kind === 'tuplet' ? tupletTicks(event, tpq) : durationTicks(event.duration, tpq)

const timeSigTicks = (timeSig: TimeSignature, tpq: number): number =>
  timeSig.beats * tpq * 4 / timeSig.unit

export interface MeasureInfo {
  measure: Measure
  index: number
  timeSig: TimeSignature
  key: KeySignature
  /** Ticks from the start of the score (written order, no repeats). */
  startTick: number
  lengthTicks: number
}

const FALLBACK_TIME_SIG: TimeSignature = { id: '', kind: 'time-signature', beats: 4, unit: 4 }
const FALLBACK_KEY: KeySignature = { id: '', kind: 'key-signature', accidentals: 0 }

/**
 * Resolves the time signature and key in effect for every measure, plus its position.
 * Falls back to 4/4 and C major if the first measure has none (the validator reports that).
 */
export const measureTimeline = (music: MusicData): MeasureInfo[] => {
  let timeSig = FALLBACK_TIME_SIG
  let key = FALLBACK_KEY
  let startTick = 0
  return music.measures.map((measure, index) => {
    timeSig = measure.timeSig ?? timeSig
    key = measure.key ?? key
    const lengthTicks = measure.actualTicks ?? timeSigTicks(timeSig, music.tpq)
    const info = { measure, index, timeSig, key, startTick, lengthTicks }
    startTick += lengthTicks
    return info
  })
}

export interface TimedEvent {
  event: VoiceEvent
  /** Start tick relative to the start of the walk (usually the measure start). */
  tick: number
  /** Actual length in ticks, after tuplet scaling. */
  ticks: number
  /** Enclosing tuplets, outermost first. */
  tuplets: Tuplet[]
}

/**
 * Lists every event of a voice with its real start and length, descending into tuplets.
 * A tuplet itself is listed before its contents.
 */
export const timeVoice = (events: VoiceEvent[], tpq: number, start = 0): TimedEvent[] => {
  const result: TimedEvent[] = []
  const walk = (list: VoiceEvent[], from: number, scale: number, tuplets: Tuplet[]): number => {
    let tick = from
    for (const event of list) {
      const ticks = eventTicks(event, tpq) * scale
      result.push({ event, tick, ticks, tuplets })
      if (event.kind === 'tuplet')
        walk(event.events, tick, scale * event.normal / event.actual, [...tuplets, event])
      tick += ticks
    }
    return tick
  }
  walk(events, start, 1, [])
  return result
}

/** Total real length of a voice's events. */
export const voiceTicks = (events: VoiceEvent[], tpq: number): number =>
  events.reduce((sum, event) => sum + eventTicks(event, tpq), 0)
