/**
 * Everything playback needs besides the score itself.
 */
import type { MusicEventOverride } from '@/core/types/EventOverride'
import type InstrumentProfile from '@/core/types/InstrumentProfile'
import type InterpretationProfile from '@/core/types/InterpretationProfile'

export interface TechniqueDefinition {
  id: string
  /** Techniques in one group replace each other ("pizz." ends at "arco"). */
  group?: string
  /** Used by an instrument that has no mapping for this technique. */
  fallback?: string
}

/** How one sheet track is played: how it is read, and the DAW tracks it sounds on. */
export interface TrackPlayback {
  interpretationProfileId: string
  outputs: TrackOutput[] // one per DAW track; most sheet tracks have one
}

/**
 * One DAW track a sheet track plays on. A drum kit written in one part has one output
 * per instrument; a snare part can send its rim clicks to a sticks patch.
 */
export interface TrackOutput {
  id: string // stays the same, so the DAW track can be found again: 'main', 'toms'
  name?: string // DAW track name; default: the sheet track's name
  instrumentProfileId: string
  sounds?: Record<string, string> // the score's kit sounds it plays → the profile's kit
  // sound each one is ('low-tom' → 'tom-4'); default: every sound no other output takes,
  // under its own name, and all pitched notes
}

export interface PlaybackSetup {
  techniques: TechniqueDefinition[]
  interpretationProfiles: InterpretationProfile[]
  instrumentProfiles: InstrumentProfile[]
  trackProfiles: Record<string, TrackPlayback> // by sheet track
  overrides: MusicEventOverride[]
}
