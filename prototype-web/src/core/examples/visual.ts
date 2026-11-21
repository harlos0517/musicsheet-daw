/**
 * A setup for driving visual effects rather than a sample library: every note as a plain
 * MIDI note, with the written dynamics on both velocity and CC1, and every tremolo
 * written out as repeated notes, so that each stroke is an event to react to.
 */
import { orchestralTechniques } from '@/core/examples/cinematicStudio'
import { defaultIntent } from '@/core/examples/profiles'
import type { ImportResult } from '@/core/musicxml/import'
import type InstrumentProfile from '@/core/types/InstrumentProfile'
import type InterpretationProfile from '@/core/types/InterpretationProfile'
import type MusicData from '@/core/types/MusicData'
import type { PlaybackSetup } from '@/core/types/PlaybackSetup'

/** Length of each note an unmeasured tremolo is written out in. */
const TREMOLO_NOTE_MS = 60

const DYNAMICS = { low: 1, high: 127 }

/** The default reading, with tremolos written out: measured by their strokes. */
export const visualIntent: InterpretationProfile = {
  id: 'visual',
  name: 'Visual effects',
  extends: 'default',
  expandTremolo: { unmeasuredMs: TREMOLO_NOTE_MS },
  noteRules: [],
  curveRules: [],
}

/** Plain notes on channel 1: dynamics as velocity at each note, and CC1 throughout. */
export const visualInstrument: InstrumentProfile = {
  id: 'visual',
  name: 'Visual effects (velocity + CC1)',
  trackDelayMs: 0,
  defaultChannel: 1,
  velocity: { mapping: DYNAMICS },
  techniqueMappings: [],
  curveMappings: [{
    parameter: 'dynamics',
    defaultValue: 0.5,
    cc: [{ number: 1, mapping: DYNAMICS }],
  }],
}

/**
 * Every part on the visual profile. Unpitched parts play their sounds on the General
 * MIDI notes the file gives them, each part on a profile of its own.
 */
export const visualSetup = (
  music: MusicData,
  kitNotes: ImportResult['kitNotes'] = {},
): PlaybackSetup => {
  const instrumentProfiles: InstrumentProfile[] = [visualInstrument]
  const trackProfiles: PlaybackSetup['trackProfiles'] = {}
  for (const track of music.tracks) {
    let profileId = visualInstrument.id
    const kit = kitNotes[track.id]
    if (kit && Object.keys(kit).length > 0) {
      profileId = `visual-${track.id}`
      instrumentProfiles.push({ ...visualInstrument, id: profileId, kit })
    }
    trackProfiles[track.id] = {
      interpretationProfileId: visualIntent.id,
      outputs: [{ id: 'main', instrumentProfileId: profileId }],
    }
  }
  return {
    techniques: orchestralTechniques,
    interpretationProfiles: [defaultIntent, visualIntent],
    instrumentProfiles,
    trackProfiles,
    overrides: [],
  }
}
