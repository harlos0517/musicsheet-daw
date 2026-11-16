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
  tracks: { trackId: string, name: string, messages: MidiMessage[] }[]
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
    const profileId = setup.trackProfiles[track.id]?.instrumentProfileId
    const profile = setup.instrumentProfiles.find(p => p.id === profileId)
    if (!profile) {
      warnings.push(`track ${track.id}: no instrument profile, skipped`)
      continue
    }
    const own = events.filter(event => event.trackId === track.id)
    const rendered = renderTrack(own, profile, tempo, setup.techniques, music.tpq)
    warnings.push(...rendered.warnings)
    tracks.push({ trackId: track.id, name: track.instrumentName, messages: rendered.messages })
  }

  const file = writeMidiFile(music.title, music.tpq, tempo, played, tracks)
  return { played, timeline, events, tempo, tracks, warnings, file }
}
