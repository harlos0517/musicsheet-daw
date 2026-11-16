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

export interface PlaybackSetup {
  techniques: TechniqueDefinition[]
  interpretationProfiles: InterpretationProfile[]
  instrumentProfiles: InstrumentProfile[]
  trackProfiles: Record<string, { interpretationProfileId: string, instrumentProfileId: string }>
  overrides: MusicEventOverride[]
}
