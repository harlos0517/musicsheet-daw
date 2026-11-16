import type MusicData from '@/core/types/MusicData'
import type { Chord, ElementAnchor, TickAnchor, VoiceEvent } from '@/core/types/MusicData'
import type { MeasureInfo } from '@/core/validation/time'
import { timeVoice } from '@/core/validation/time'

/** Where a voice event sits in the written score. */
export interface Placed<E extends VoiceEvent = VoiceEvent> {
  event: E
  trackId: string
  staffId: string
  voiceIndex: number
  measureIndex: number
  /** Tick inside the measure. */
  tick: number
  ticks: number
  /** Written-order tick from the start of the score (no repeats). */
  absTick: number
}

export interface Position { measureIndex: number, tick: number }

export interface ScoreIndex {
  /** Chords, rests, spaces and tuplets by id; notes map to their chord. */
  elements: Map<string, Placed>
  /** Every chord of a track, in written order. */
  chords: Map<string, Placed<Chord>[]>
  /** Voice indexes used anywhere on a staff. */
  voices: Map<string, number[]>
  measureIndex: Map<string, number>
}

export const indexScore = (music: MusicData, timeline: MeasureInfo[]): ScoreIndex => {
  const measureIndex = new Map(music.measures.map((measure, index) => [measure.id, index]))
  const elements = new Map<string, Placed>()
  const chords = new Map<string, Placed<Chord>[]>()
  const voices = new Map<string, number[]>()

  for (const track of music.tracks) {
    const trackChords: Placed<Chord>[] = []
    for (const staff of track.staves) {
      const staffVoices = new Set<number>()
      for (const staffMeasure of staff.measures) {
        const index = measureIndex.get(staffMeasure.measureId)
        if (index === undefined) continue
        for (const voice of staffMeasure.voices) {
          staffVoices.add(voice.index)
          for (const timed of timeVoice(voice.events, music.tpq)) {
            const placed: Placed = {
              event: timed.event,
              trackId: track.id,
              staffId: staff.id,
              voiceIndex: voice.index,
              measureIndex: index,
              tick: timed.tick,
              ticks: timed.ticks,
              absTick: timeline[index]!.startTick + timed.tick,
            }
            elements.set(timed.event.id, placed)
            if (timed.event.kind !== 'chord') continue
            trackChords.push(placed as Placed<Chord>)
            for (const note of timed.event.notes) elements.set(note.id, placed)
          }
        }
      }
      voices.set(staff.id, [...staffVoices].sort((a, b) => a - b))
    }
    chords.set(track.id, trackChords.sort((a, b) => a.absTick - b.absTick))
  }
  return { elements, chords, voices, measureIndex }
}

/** `atEnd` puts an element anchor at the end of its element instead of the start. */
export const resolveAnchor = (
  index: ScoreIndex,
  anchor: ElementAnchor | TickAnchor,
  atEnd = false,
): Position | undefined => {
  if (anchor.type === 'tick') {
    const measure = index.measureIndex.get(anchor.measureId)
    return measure === undefined ? undefined : { measureIndex: measure, tick: anchor.tick }
  }
  const placed = index.elements.get(anchor.id)
  if (!placed) return undefined
  return { measureIndex: placed.measureIndex, tick: placed.tick + (atEnd ? placed.ticks : 0) }
}

export const absTick = (timeline: MeasureInfo[], position: Position): number =>
  timeline[position.measureIndex]!.startTick + position.tick
