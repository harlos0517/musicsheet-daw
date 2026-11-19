/**
 * Example profiles and setups for turning the example scores into MIDI.
 * The controller and keyswitch numbers are made up; they stand in for a sample library.
 */
import type InstrumentProfile from '@/core/types/InstrumentProfile'
import type InterpretationProfile from '@/core/types/InterpretationProfile'
import type { DynamicMapping } from '@/core/types/InterpretationProfile'
import { ArticulationType, DynamicType } from '@/core/types/MusicData'
import type { PlaybackSetup, TechniqueDefinition } from '@/core/types/PlaybackSetup'

export const techniques: TechniqueDefinition[] = [
  { id: 'core:staccato' },
  { id: 'core:accent' },
  { id: 'core:legato', group: 'core:phrasing' },
  // Switch how unslurred notes are read from here on (cantabile, maestoso, ...).
  { id: 'core:connected', group: 'core:phrasing-mode' },
  { id: 'core:detached', group: 'core:phrasing-mode' },
  { id: 'core:pizz', group: 'core:bowing' },
  { id: 'core:arco', group: 'core:bowing' },
  { id: 'core:soft-bow' },
  { id: 'user:sul-tasto', fallback: 'core:soft-bow' },
  { id: 'user:molto-vibrato' },
]

const level = (type: DynamicType, value: number) => ({ type, mode: 'set' as const, value })

/** A peak `value` above the current level that fades back to it within `afterQn`. */
const accent = (type: DynamicType, value: number, afterQn: number): DynamicMapping => ({
  type,
  mode: 'add',
  combine: 'add',
  value,
  then: { afterQn, mode: 'add', value: 0, curveType: 'linear' },
})

/** Sensible defaults for any instrument. */
export const defaultIntent: InterpretationProfile = {
  id: 'default',
  name: 'Default',
  initialDynamics: 0.5,
  graceNotes: {
    // On the beat, taking half its written length out of the main note.
    appoggiatura: { ratio: 0.5 },
    // Before the beat, a quarter of its written length (a written eighth → a 32nd).
    acciaccatura: { ratio: 0.25 },
  },
  dynamics: [
    level(DynamicType.PPPP, 0.05),
    level(DynamicType.PPP, 0.10),
    level(DynamicType.PP, 0.15),
    level(DynamicType.P, 0.20),
    level(DynamicType.MP, 0.40),
    level(DynamicType.MF, 0.60),
    level(DynamicType.F, 0.80),
    level(DynamicType.FF, 0.85),
    level(DynamicType.FFF, 0.90),
    level(DynamicType.FFFF, 0.95),
    // Accents: added on top of the level, then back to it.
    accent(DynamicType.SFZ, 0.3, 0.5),
    accent(DynamicType.SF, 0.2, 0.5),
    accent(DynamicType.RFZ, 0.25, 1),
    // Loud, then down to piano within a sixteenth.
    {
      type: DynamicType.FP,
      mode: 'set',
      value: 0.7,
      then: { afterQn: 0.25, mode: 'set', value: 0.35, curveType: 'exp' },
    },
  ],
  hairpin: { defaultChange: 0.2 }, // linear
  noteRules: [
    // Durations are set as a share of the notated length, so a later rule replaces an
    // earlier one instead of multiplying with it.
    {
      id: 'detached',
      match: { kind: 'any' },
      actions: [{ op: 'setRatio', for: 'duration', value: 0.9 }],
    },
    {
      id: 'slur',
      match: { kind: 'slur' },
      actions: [{ op: 'addTechnique', techniqueId: 'core:legato' }],
    },
    {
      id: 'slur-first',
      match: { kind: 'slur', position: 'first' },
      actions: [{ op: 'setRatio', for: 'duration', value: 1 }],
    },
    {
      id: 'slur-inner',
      match: { kind: 'slur', position: 'inner' },
      actions: [{ op: 'setRatio', for: 'duration', value: 1 }],
    },
    {
      id: 'staccato',
      match: { kind: 'articulation', type: ArticulationType.STACCATO },
      actions: [
        { op: 'setRatio', for: 'duration', value: 0.5 },
        { op: 'addTechnique', techniqueId: 'core:staccato' },
      ],
    },
    {
      id: 'staccatissimo',
      match: { kind: 'articulation', type: ArticulationType.STACCATISSIMO },
      actions: [
        { op: 'setRatio', for: 'duration', value: 0.25 },
        { op: 'addTechnique', techniqueId: 'core:staccato' },
      ],
    },
    {
      id: 'accent',
      match: { kind: 'articulation', type: ArticulationType.ACCENT },
      actions: [
        { op: 'add', for: 'velocity', value: 0.15 },
        { op: 'addTechnique', techniqueId: 'core:accent' },
      ],
    },
    {
      id: 'grace',
      match: { kind: 'grace' },
      actions: [{ op: 'scale', for: 'velocity', value: 0.8 }],
    },
  ],
  curveRules: [
    {
      id: 'sustain-pedal',
      match: { kind: 'pedal', type: 'sustain' },
      parameter: 'sustain',
      startValue: 0,
      endValue: 1,
      points: [{ time: 0, value: 1 }, { time: 1, value: 0, curveType: 'hold' }],
    },
    {
      id: 'soft-pedal',
      match: { kind: 'pedal', type: 'una-corda' },
      parameter: 'una-corda',
      startValue: 0,
      endValue: 1,
      points: [{ time: 0, value: 1 }, { time: 1, value: 0, curveType: 'hold' }],
    },
  ],
}

/** Piano: the pedal connects notes, so slurs do nothing and notes keep their full length. */
export const pianoIntent: InterpretationProfile = {
  id: 'piano',
  name: 'Piano',
  extends: 'default',
  noteRules: [
    {
      id: 'detached',
      match: { kind: 'any' },
      actions: [{ op: 'setRatio', for: 'duration', value: 1 }],
    },
    { id: 'slur', enabled: false, match: { kind: 'slur' }, actions: [] },
    { id: 'slur-first', enabled: false, match: { kind: 'slur' }, actions: [] },
    { id: 'slur-inner', enabled: false, match: { kind: 'slur' }, actions: [] },
  ],
  curveRules: [],
}

/** Strings: a louder sfz, and a custom "molto vib." line that drives a vibrato curve. */
export const stringsIntent: InterpretationProfile = {
  id: 'strings',
  name: 'Strings',
  extends: 'default',
  dynamics: [accent(DynamicType.SFZ, 0.4, 0.5)],
  noteRules: [],
  curveRules: [
    {
      id: 'molto-vibrato',
      match: { kind: 'technique', techniqueId: 'user:molto-vibrato' },
      parameter: { kind: 'custom', name: 'vibrato' },
      startValue: 0.3,
      endValue: 1,
      points: [
        { time: 0, value: 0 },
        { time: 0.5, value: 1, curveType: 'ease' },
        { time: 1, value: 0, curveType: 'hold' },
      ],
    },
  ],
}

const fullVelocity = { mapping: { low: 1, high: 127 } }
const key = (note: number) => ({ note, velocity: 100 })

export const pianoInstrument: InstrumentProfile = {
  id: 'piano',
  name: 'Piano',
  trackDelayMs: 0,
  velocity: fullVelocity,
  defaultChannel: 1,
  techniqueMappings: [],
  curveMappings: [
    { parameter: 'sustain', defaultValue: 0, cc: [{ number: 64, mapping: { low: 0, high: 127 } }] },
    {
      parameter: 'una-corda',
      defaultValue: 0,
      cc: [{ number: 67, mapping: { low: 0, high: 127 } }],
    },
  ],
}

/** One patch with keyswitches; loudness comes from CC1, velocity only shapes the attack. */
export const windInstrument: InstrumentProfile = {
  id: 'wind',
  name: 'Keyswitched wind',
  trackDelayMs: -60,
  velocity: { mapping: { low: 40, high: 110 } },
  defaultChannel: 2,
  keyswitchLeadMs: 20,
  techniqueMappings: [
    { matches: [], keyswitch: key(24) }, // sustain; matches every note
    { matches: [{ kind: 'technique', techniqueId: 'core:staccato' }], keyswitch: key(25) },
    {
      matches: [{ kind: 'technique', techniqueId: 'core:legato' }],
      keyswitch: key(26),
    },
    {
      // Legato transitions need a head start, and more of it when playing softly:
      // this mapping and the next one add up to -50 ms below mezzo.
      matches: [{ kind: 'slur', position: 'inner' }],
      startOffsetMs: -30,
    },
    {
      matches: [
        { kind: 'slur', position: 'inner' },
        { kind: 'curve', parameter: 'dynamics', min: 0, max: 0.5 },
      ],
      startOffsetMs: -20,
    },
    {
      matches: [{ kind: 'technique', techniqueId: 'core:accent' }],
      velocity: { mode: 'add', value: 0.1 },
    },
  ],
  curveMappings: [
    {
      parameter: 'dynamics',
      defaultValue: 0.5,
      cc: [
        { number: 1, mapping: { low: 0, high: 127 } },
        { number: 11, mapping: { low: 64, high: 127 } },
      ],
    },
  ],
}

export const drumInstrument: InstrumentProfile = {
  id: 'drums',
  name: 'Drum kit',
  trackDelayMs: 0,
  velocity: fullVelocity,
  defaultChannel: 10,
  techniqueMappings: [],
  curveMappings: [],
  kit: { 'kick': 36, 'snare': 38, 'hihat-closed': 42 },
}

/**
 * One articulation per channel, with an extra layer for accents:
 * channel 4 arco, channel 5 pizzicato, channel 6 a short attack layer added on top.
 */
export const violinInstrument: InstrumentProfile = {
  id: 'violin',
  name: 'Layered violin',
  trackDelayMs: -80,
  velocity: { mapping: { low: 30, high: 120 } },
  defaultChannel: 4,
  techniqueMappings: [
    { matches: [], cc: [{ number: 20, value: 0 }] }, // base: normal bow
    { matches: [{ kind: 'technique', techniqueId: 'core:pizz' }], channels: [5] },
    {
      matches: [{ kind: 'technique', techniqueId: 'core:accent' }],
      channelMode: 'add',
      channels: [6],
      velocity: { mode: 'scale', value: 0.6 },
      durationMs: { mode: 'set', value: 120 },
    },
    {
      matches: [{ kind: 'technique', techniqueId: 'core:soft-bow' }],
      cc: [{ number: 20, value: 100 }],
    },
    {
      matches: [{ kind: 'technique', techniqueId: 'core:legato' }],
      durationMs: { mode: 'add', value: 40 },
    },
  ],
  curveMappings: [
    {
      parameter: 'dynamics',
      defaultValue: 0.5,
      cc: [{ number: 1, mapping: { low: 0, high: 127 } }],
    },
    {
      parameter: { kind: 'custom', name: 'vibrato' },
      defaultValue: 0.3,
      channels: [4],
      cc: [{ number: 21, mapping: { low: 0, high: 127 } }],
      keyswitch: [
        { min: 0, max: 0.6, note: 12, velocity: 100 }, // normal vibrato patch
        { min: 0.6, max: 1, note: 13, velocity: 100 }, // molto vibrato patch
      ],
    },
  ],
}

const shared = {
  techniques,
  interpretationProfiles: [defaultIntent, pianoIntent, stringsIntent],
  instrumentProfiles: [pianoInstrument, windInstrument, drumInstrument, violinInstrument],
}

/** Playback setup for each example score, keyed like `examples`. */
export const setups: Record<'melody' | 'piano' | 'band' | 'strings', PlaybackSetup> = {
  melody: {
    ...shared,
    trackProfiles: {
      'mel-flute': {
        interpretationProfileId: 'default',
        outputs: [{ id: 'main', instrumentProfileId: 'wind' }],
      },
    },
    overrides: [],
  },
  piano: {
    ...shared,
    trackProfiles: { pno: {
      interpretationProfileId: 'piano',
      outputs: [{ id: 'main', instrumentProfileId: 'piano' }],
    } },
    overrides: [],
  },
  band: {
    ...shared,
    trackProfiles: {
      'band-cl': {
        interpretationProfileId: 'default',
        outputs: [{ id: 'main', instrumentProfileId: 'wind' }],
      },
      'band-dr': {
        interpretationProfileId: 'default',
        outputs: [{ id: 'main', instrumentProfileId: 'drums' }],
      },
    },
    overrides: [],
  },
  strings: {
    ...shared,
    trackProfiles: { vln: {
      interpretationProfileId: 'strings',
      outputs: [{ id: 'main', instrumentProfileId: 'violin' }],
    } },
    overrides: [
      {
        // Hold the last note back a little and let it ring longer.
        id: 'ovr-last-note',
        kind: 'note-event-override',
        sourceId: 'vln-last-note',
        startTickOffset: 60,
        velocity: { mode: 'scale', value: 0.9 },
      },
      {
        // Stop the final diminuendo short of silence.
        id: 'ovr-dim',
        kind: 'curve-event-override',
        sourceId: 'vln-dim',
        segmentIndex: 0,
        toValue: { mode: 'set', value: 0.1 },
      },
    ],
  },
}
