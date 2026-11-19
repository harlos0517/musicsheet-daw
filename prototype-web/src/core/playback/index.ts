import type MusicData from '@/core/types/MusicData'
import type { MusicEvent } from '@/core/types/MusicEvent'
import type { PlayedMeasure } from '@/core/validation/repeats'
import type { MeasureInfo } from '@/core/validation/time'
import { measureTimeline } from '@/core/validation/time'
import type { PlaybackSetup } from '../types/PlaybackSetup'
import { buildMusicEvents } from './buildEvents'
import { writeMidiFile } from './midiFile'
import type { MidiMessage } from './renderMidi'
import { renderTrack } from './renderMidi'
import { indexScore } from './scoreIndex'
import type { TempoMap } from './tempo'
import { buildTempoMap } from './tempo'

export interface Playback {
  played: PlayedMeasure[]
  timeline: MeasureInfo[]
  events: MusicEvent[]
  tempo: TempoMap
  /** One per output: a sheet track can play on several DAW tracks. */
  tracks: { trackId: string, outputId: string, name: string, messages: MidiMessage[] }[]
  warnings: string[]
  file: Uint8Array
}

/** The whole chain: score + setup → MusicEvents → MIDI messages → a MIDI file. */
export const play = (music: MusicData, setup: PlaybackSetup): Playback => {
  const timeline = measureTimeline(music)
  const { played, events, warnings } = buildMusicEvents(music, setup)
  const tempo = buildTempoMap(music, played, indexScore(music, timeline), timeline)

  const tracks: Playback['tracks'] = []
  for (const track of music.tracks) {
    const outputs = setup.trackProfiles[track.id]?.outputs ?? []
    if (outputs.length === 0) warnings.push(`track ${track.id}: no outputs, skipped`)
    const own = events.filter(event => event.trackId === track.id)
    const soundOf = (event: MusicEvent) => event.kind === 'note-event'
      && typeof event.pitch !== 'number' ? event.pitch.sound : undefined
    // Sounds an output names are its own; the output without a map plays the rest.
    const taken = new Set(outputs.flatMap(output => Object.keys(output.sounds ?? {})))
    if (outputs.length > 0 && outputs.every(output => output.sounds)) {
      const unplayed = new Set(own.flatMap(event => {
        const sound = soundOf(event)
        return sound !== undefined && !taken.has(sound) ? [sound] : []
      }))
      for (const sound of unplayed)
        warnings.push(`track ${track.id}: no output plays the sound "${sound}"`)
    }

    for (const output of outputs) {
      const profile = setup.instrumentProfiles.find(p => p.id === output.instrumentProfileId)
      if (!profile) {
        warnings.push(`track ${track.id}, output ${output.id}: no instrument profile, skipped`)
        continue
      }
      const map = output.sounds
      const mine = own.flatMap((event): MusicEvent[] => {
        if (event.kind !== 'note-event') return [event] // the curves go with every output
        const sound = soundOf(event)
        if (map) {
          // The score's sound, under the name the patch knows it by.
          const kitSound = sound === undefined ? undefined : map[sound]
          return kitSound === undefined ? [] : [{ ...event, pitch: { sound: kitSound } }]
        }
        return sound === undefined || !taken.has(sound) ? [event] : []
      })
      // Of several outputs, one with nothing to play makes no empty track.
      if (outputs.length > 1 && !mine.some(event => event.kind === 'note-event')) continue
      const rendered = renderTrack(mine, profile, tempo, setup.techniques, music.tpq)
      warnings.push(...rendered.warnings)
      tracks.push({
        trackId: track.id,
        outputId: output.id,
        name: output.name ?? track.instrumentName,
        messages: rendered.messages,
      })
    }
  }

  const file = writeMidiFile(music.title, music.tpq, tempo, played, tracks)
  return { played, timeline, events, tempo, tracks, warnings, file }
}
