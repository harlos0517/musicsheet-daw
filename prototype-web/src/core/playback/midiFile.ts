import type { PlayedMeasure } from '@/core/validation/repeats'
import type { MidiMessage } from './renderMidi'
import { messageOrder } from './renderMidi'
import type { TempoMap } from './tempo'

export interface MidiFileTrack { name: string, messages: MidiMessage[] }

const text = (value: string): number[] => [...new TextEncoder().encode(value)]

const uint = (value: number, bytes: number): number[] =>
  Array.from({ length: bytes }, (_, i) => (value >>> (8 * (bytes - 1 - i))) & 0xff)

/** MIDI variable-length quantity. */
const vlq = (value: number): number[] => {
  const result = [value & 0x7f]
  for (let rest = value >>> 7; rest > 0; rest >>>= 7) result.unshift((rest & 0x7f) | 0x80)
  return result
}

const meta = (type: number, data: number[]): number[] => [0xff, type, ...vlq(data.length), ...data]

const chunk = (id: string, data: number[]): number[] =>
  [...text(id), ...uint(data.length, 4), ...data]

const trackChunk = (events: { tick: number, bytes: number[] }[]): number[] => {
  const data: number[] = []
  let last = 0
  for (const event of events) {
    const tick = Math.max(last, Math.round(event.tick))
    data.push(...vlq(tick - last), ...event.bytes)
    last = tick
  }
  data.push(0, ...meta(0x2f, []))
  return chunk('MTrk', data)
}

const STATUS = { 'note-off': 0x80, 'note-on': 0x90, 'cc': 0xb0 }

/**
 * Standard MIDI file, format 1: a conductor track with tempo and time signatures, then
 * one track per instrument. Message times go back from milliseconds to ticks through the
 * tempo map, so the file keeps its bars and tempo when opened in a DAW.
 */
export const writeMidiFile = (
  title: string,
  tpq: number,
  tempo: TempoMap,
  played: PlayedMeasure[],
  tracks: MidiFileTrack[],
): Uint8Array => {
  const conductor: { tick: number, bytes: number[] }[] = [
    { tick: 0, bytes: meta(0x03, text(title)) },
  ]
  let signature = ''
  for (const measure of played) {
    const { beats, unit } = measure.info.timeSig
    if (signature === `${beats}/${unit}`) continue
    signature = `${beats}/${unit}`
    conductor.push({
      tick: measure.startTick,
      bytes: meta(0x58, [beats, Math.log2(unit), 24, 8]),
    })
  }
  for (const point of tempo.points) {
    const microsPerQuarter = Math.round(60000000 / point.qpm)
    conductor.push({ tick: point.tick, bytes: meta(0x51, uint(microsPerQuarter, 3)) })
  }

  conductor.sort((a, b) => a.tick - b.tick)

  const chunks = [trackChunk(conductor)]
  for (const track of tracks) {
    const events = track.messages
      .map(message => ({
        tick: Math.round(tempo.msToTick(message.timeMs)),
        order: messageOrder(message),
        bytes: [STATUS[message.type] | (message.channel - 1), message.data1, message.data2],
      }))
      .sort((a, b) => a.tick - b.tick || a.order - b.order)
    chunks.push(trackChunk([{ tick: 0, bytes: meta(0x03, text(track.name)) }, ...events]))
  }

  const header = chunk('MThd', [...uint(1, 2), ...uint(chunks.length, 2), ...uint(tpq, 2)])
  return Uint8Array.from([...header, ...chunks.flat()])
}
