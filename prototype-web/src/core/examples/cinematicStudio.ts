/**
 * Profiles for the Cinematic Studio libraries (Strings, Woodwinds, Brass), built from the
 * rules in `rules/cinematic-studio.md`, and a setup that assigns them to the parts of an
 * imported score. Each block below names the rule it comes from.
 *
 * The patches are expected with legato on and the marcato repetition overlay on.
 * Articulations are chosen with keyswitch notes; dynamics go to CC1.
 */
import type { ImportResult } from '@/core/musicxml/import'
import type InstrumentProfile from '@/core/types/InstrumentProfile'
import type { NoteMatch, TechniqueMapping } from '@/core/types/InstrumentProfile'
import type InterpretationProfile from '@/core/types/InterpretationProfile'
import type MusicData from '@/core/types/MusicData'
import { ArticulationType, DynamicType } from '@/core/types/MusicData'
import type { PlaybackSetup, TechniqueDefinition } from '@/core/types/PlaybackSetup'
import { defaultIntent, techniques as exampleTechniques } from './profiles'
import {
  percussionFor, percussionIntents, percussionProfiles, percussionTechniques,
} from './symphonySeries'

// const KEY_SWITCH_CC = 58
const DYNAMICS_CC = 1
/**
 * Slurred notes written this short or shorter, in real time, are played with the marcato
 * articulation, which is what these libraries use for runs. Measured in milliseconds so
 * that the tempo counts: an eighth at 176 (170 ms) is as fast as a sixteenth at 88.
 */
const FAST_SLUR_MAX_MS = 160
/** How far a slurred note is held into the next one, so that the two overlap. */
const FAST_SLUR_OVERLAP_MS = 25

export const orchestralTechniques: TechniqueDefinition[] = [
  ...exampleTechniques,
  { id: 'core:staccatissimo', fallback: 'core:staccato' },
  { id: 'core:fast-legato', group: 'core:phrasing' },
  { id: 'core:tremolo' },
  // A tremolo between two notes, played as a trill; and the notes of a measured tremolo.
  { id: 'core:tremolo-pair' },
  { id: 'core:measured-tremolo' },
  { id: 'core:trill' },
  // Trills the importer knows the second note of; the engine plays both notes.
  { id: 'core:trill-half', fallback: 'core:trill' },
  { id: 'core:trill-whole', fallback: 'core:trill' },
  { id: 'core:con-sord', group: 'core:mute' },
  { id: 'core:senza-sord', group: 'core:mute' },
  { id: 'core:sul-pont', group: 'core:bow-position' },
  { id: 'core:sul-tasto', group: 'core:bow-position' },
  { id: 'core:ord', group: 'core:bow-position' },
  ...percussionTechniques,
]

const fastSlur = [
  { op: 'removeTechnique' as const, techniqueId: 'core:legato' },
  { op: 'addTechnique' as const, techniqueId: 'core:fast-legato' },
]

/** How the page is read, for every Cinematic Studio instrument. */
export const orchestralIntent: InterpretationProfile = {
  id: 'orchestral',
  name: 'Orchestral',
  extends: 'default',
  // Long notes and slow slurs swell in and out: from 75 % of the written level, over a
  // quarter of a single note, or half of the first and last note of a slur.
  swell: {
    minMs: FAST_SLUR_MAX_MS,
    // A slur swells unless it is a fast run (played with marcato).
    slurMinMs: FAST_SLUR_MAX_MS,
    startLevel: 0.75,
    noteRatio: 0.25,
    slurRatio: 0.5,
    // Ease out of the start (quick, then settling) and into the end (gentle, then quicker).
    // Power: steepness below 0 starts fast, above 0 starts slow.
    rise: { curveType: 'power', steepness: -1 },
    fall: { curveType: 'power', steepness: 1 },
  },
  // Hairpins, exponential and mirrored: a cresc. grows slowly, then quicker; a dim. drops
  // quickly, then settles. Exp: steepness above 0 starts slow, below 0 starts fast.
  hairpin: {
    cresc: { curveType: 'exp', steepness: 1 },
    dim: { curveType: 'exp', steepness: -1 },
    defaultChange: 0.2,
  },
  // sfp / fp: CC1 starts at piano right away; the attack comes from the note (rule below).
  dynamics: [{ type: DynamicType.FP, mode: 'set', value: 0.2 }],
  noteRules: [
    // Detached: 0.95 of the written length. Also the last note of any slur.
    {
      id: 'detached',
      match: { kind: 'any' },
      actions: [{ op: 'setRatio', for: 'duration', value: 0.95 }],
    },
    {
      id: 'staccatissimo',
      match: { kind: 'articulation', type: ArticulationType.STACCATISSIMO },
      actions: [
        { op: 'setRatio', for: 'duration', value: 0.25 },
        { op: 'addTechnique', techniqueId: 'core:staccatissimo' },
      ],
    },
    // Fast slurs: when this note or the one before it is short enough.
    {
      id: 'fast-slur',
      match: { kind: 'slur' },
      when: { maxNotatedMs: FAST_SLUR_MAX_MS },
      actions: fastSlur,
    },
    {
      id: 'fast-slur-after-short',
      match: { kind: 'slur' },
      when: { maxPreviousNotatedMs: FAST_SLUR_MAX_MS },
      actions: fastSlur,
    },
    // Accent / marcato: dynamics plus 0.15.
    {
      id: 'accent',
      match: { kind: 'articulation', type: ArticulationType.ACCENT },
      actions: [
        { op: 'add', for: 'velocity', value: 0.15 },
        { op: 'addTechnique', techniqueId: 'core:accent' },
      ],
    },
    // sfp: a loud marcato attack over a soft CC1.
    {
      id: 'sfp',
      match: { kind: 'dynamic', type: DynamicType.FP },
      actions: [
        { op: 'set', for: 'velocity', value: 0.9 },
        { op: 'addTechnique', techniqueId: 'core:accent' },
      ],
    },
    // An accented note that opens a slur is a marcato attack at its own velocity, not the
    // soft start of a fast run. The notes after it decide for themselves.
    {
      id: 'accent-opens-slur',
      match: { kind: 'technique', techniqueId: 'core:accent' },
      when: { slurPosition: 'first' },
      actions: [{ op: 'removeTechnique', techniqueId: 'core:fast-legato' }],
    },
    {
      id: 'tenuto',
      match: { kind: 'articulation', type: ArticulationType.TENUTO },
      actions: [{ op: 'setRatio', for: 'duration', value: 0.99 }],
    },
    // Staccato at the end of a slur: keep the slur's articulation, half length.
    ...[ArticulationType.STACCATO, ArticulationType.STACCATISSIMO].map(type => ({
      id: `${type}-ends-slur`,
      match: { kind: 'articulation' as const, type },
      when: { slurPosition: 'last' as const },
      actions: [
        { op: 'removeTechnique' as const, techniqueId: 'core:staccato' },
        { op: 'removeTechnique' as const, techniqueId: 'core:staccatissimo' },
        { op: 'setRatio' as const, for: 'duration' as const, value: 0.5 },
      ],
    })),
  ],
  curveRules: [],
}

/** The same, with unslurred notes that touch played as if slurred. Used for every family. */
export const connectedIntent: InterpretationProfile = {
  id: 'orchestral-connected',
  name: 'Orchestral, connected',
  extends: 'orchestral',
  defaultPhrasing: 'connected',
  noteRules: [],
  curveRules: [],
}

const technique = (techniqueId: string): NoteMatch => ({ kind: 'technique', techniqueId })
/** Typed, so a misspelt field is an error: a spread object is not checked otherwise. */
const ks = (note: number, velocity = 127): Pick<TechniqueMapping, 'keyswitch'> =>
  ({ keyswitch: { note, velocity } })

interface Library {
  id: string
  name: string
  /** How far ahead the inner and last notes of a slow slur come in. */
  slowLegatoMs: number
  /** Articulations only this library has. */
  extra?: TechniqueMapping[]
  velocity?: InstrumentProfile['velocity']
}

const SUSTAIN = ks(24)
const MARCATO = ks(30)
const STACCATO = ks(29, 40)
/** Measured tremolo (strings), measured repetitions (woodwinds), double tongue (brass). */
const MEASURED_TREMOLO = ks(26)
const TRILL = ks(27)
/** Share of its played length a double-tongued note is held for, in the brass. */
const DOUBLE_TONGUE_LENGTH = 0.4
/** Flutter tongue for woodwinds and brass. */
const FLUTTER = ks(28)
/** Velocity of slurred notes after the first: it sets how fast the transition is. */
const SLUR_VELOCITY = 0.75
/**
 * Short notes: CC1 picks the kind of short note, lower for shorter. The lengths and
 * values here are a first guess, to be tuned by ear.
 */
const SHORT_BY_LENGTH = { shortMs: 150, longMs: 800, atShort: 0, atLong: 127 }

const cinematic = (library: Library): InstrumentProfile => ({
  id: library.id,
  name: library.name,
  trackDelayMs: -66,
  defaultChannel: 1,
  // Velocity follows dynamics unless a rule below fixes it.
  velocity: library.velocity ?? { mapping: { low: 1, high: 127 } },
  keyswitchLeadMs: 0,
  techniqueMappings: [
    // Detached notes, and the start of everything else: sustain.
    { matches: [], ...SUSTAIN },

    // Slow slurs: sustain; the inner and last notes come in early at a fixed velocity.
    // The first note keeps the velocity its dynamic gives it.
    ...(['inner', 'last'] as const).map((position): TechniqueMapping => ({
      matches: [technique('core:legato'), { kind: 'slur', position }],
      velocity: { mode: 'set', value: SLUR_VELOCITY },
      startOffsetMs: -library.slowLegatoMs,
    })),

    // Accent / marcato.
    { matches: [technique('core:accent')], ...MARCATO },

    // Fast slurs: marcato; the first and inner notes end late.
    { matches: [technique('core:fast-legato')], ...MARCATO },
    {
      matches: [technique('core:fast-legato'), { kind: 'slur', position: 'first' }],
      velocity: { mode: 'set', value: 0.1 },
    },
    ...(['inner', 'last'] as const).map((position): TechniqueMapping => ({
      matches: [technique('core:fast-legato'), { kind: 'slur', position }],
      velocity: { mode: 'set', value: SLUR_VELOCITY },
    })),
    // Not in the rules: a slow note is also held into a fast one after it, or the two
    // would only touch and the legato would not trigger.
    ...(['first', 'inner'] as const).map((position): TechniqueMapping => ({
      matches: [{ kind: 'slur', position }],
      durationMs: { mode: 'add', value: FAST_SLUR_OVERLAP_MS },
    })),

    ...(library.extra ?? []),

    // Staccato / spiccato: one articulation, CC1 by note length. Staccatissimo has no
    // mapping of its own and falls back to this one.
    {
      matches: [technique('core:staccato')],
      ...STACCATO,
      cc: [{ number: DYNAMICS_CC, value: SHORT_BY_LENGTH }],
    },
    // Accent + staccato: repetitions, CC1 at 0.
    {
      matches: [technique('core:staccato'), technique('core:accent')],
      ...STACCATO,
      cc: [{ number: DYNAMICS_CC, value: 0 }],
    },

    // Tremolos, last so that they win over the articulations above.
    { matches: [technique('core:measured-tremolo')], ...MEASURED_TREMOLO },
    // Both notes of a two-note tremolo start together, which the trill patch needs.
    { matches: [technique('core:tremolo-pair')], ...TRILL },
    { matches: [technique('core:trill')], ...TRILL },
  ],
  // CC1 follows dynamics everywhere a short note does not take it over.
  curveMappings: [{
    parameter: 'dynamics',
    defaultValue: 0.5,
    cc: [{ number: DYNAMICS_CC, mapping: { low: 1, high: 127 } }],
  }],
})

/** Cinematic Studio Strings; also stands in for the saxophones. Expressive legato. */
export const cinematicStrings = cinematic({
  id: 'css',
  name: 'Cinematic Studio Strings',
  slowLegatoMs: 150,
  extra: [
    { matches: [technique('core:tremolo')], ...ks(25) },
    { matches: [technique('core:pizz')], ...ks(31) },
  ],
  velocity: { mapping: { low: 10, high: 127, curve: { curveType: 'ease', steepness: 1 } } },
})

/** Cinematic Studio Woodwinds. Expressive legato. */
export const cinematicWoodwinds = cinematic({
  id: 'csw',
  name: 'Cinematic Studio Woodwinds',
  slowLegatoMs: 75,
  extra: [{ matches: [technique('core:tremolo')], ...FLUTTER }],
  velocity: { mapping: { low: 1, high: 127, curve: { curveType: 'ease', steepness: 1 } } },
})


/** Cinematic Studio Brass, which has one sustain articulation. */
export const cinematicBrass = cinematic({
  id: 'csb',
  name: 'Cinematic Studio Brass',
  slowLegatoMs: 75,
  extra: [
    { matches: [technique('core:tremolo')], ...FLUTTER },
    // Double tongue: CC1 sets the number of repetitions, not the dynamics. At the top it
    // is "Hold", which repeats for as long as the key is down.
    // The phrase runs on after the key is let go, so the note is cut to 0.4 of its length.
    {
      matches: [technique('core:measured-tremolo')],
      cc: [{ number: DYNAMICS_CC, value: 127 }],
      durationMs: { mode: 'scale', value: DOUBLE_TONGUE_LENGTH },
    },
  ],
  velocity: { mapping: { low: 1, high: 127, curve: { curveType: 'exp', steepness: 1 } } },
})

/** Keyboards, harp and pitched percussion: plain notes, velocity for loudness. */
export const plainInstrument: InstrumentProfile = {
  id: 'plain',
  name: 'Plain (velocity only)',
  trackDelayMs: 0,
  defaultChannel: 1,
  velocity: { mapping: { low: 1, high: 127 } },
  techniqueMappings: [],
  curveMappings: [
    { parameter: 'sustain', defaultValue: 0, cc: [{ number: 64, mapping: { low: 0, high: 127 } }] },
  ],
}

/**
 * Patches that are not mapped at sounding pitch: the piccolo plays an octave lower than
 * it sounds, the tuba and the contrabass an octave higher. That is a fact of the patch,
 * the same in every score, so each is a profile of its own.
 */
export const cinematicPiccolo: InstrumentProfile = {
  ...cinematicWoodwinds, id: 'csw-piccolo', name: 'Cinematic Studio Woodwinds – Piccolo',
  pitchOffset: -12,
}
export const cinematicTuba: InstrumentProfile = {
  ...cinematicBrass, id: 'csb-tuba', name: 'Cinematic Studio Brass – Tuba', pitchOffset: 12,
}
export const cinematicContrabass: InstrumentProfile = {
  ...cinematicStrings, id: 'css-contrabass', name: 'Cinematic Studio Strings – Contrabass',
  pitchOffset: 12,
}

/** The profile of a part, by its name; tested in order, single patches first. */
const FAMILIES: [RegExp, InstrumentProfile][] = [
  [/piccolo/i, cinematicPiccolo],
  [/tuba/i, cinematicTuba],
  // Not the contrabassoon, a woodwind.
  [/contrabass(?!oon)|double bass/i, cinematicContrabass],
  // "English Horn" is a woodwind, so the woodwinds are tested before the brass.
  [/piccolo|flute|oboe|english horn|cor anglais|clarinet|bassoon/i, cinematicWoodwinds],
  [/sax/i, cinematicStrings],
  [/horn|trumpet|cornet|flugel|trombone|euphonium|baritone|tuba/i, cinematicBrass],
  [/violin|viola|cello|contrabass|double bass|strings/i, cinematicStrings],
]

/**
 * A setup for an imported score: every part gets a profile by its instrument name.
 * Percussion goes to Symphony Series (`symphonySeries.ts`), one patch per instrument; the
 * General MIDI notes the file gives unpitched sounds only help tell a drum kit apart.
 */
export const cinematicSetup = (
  music: MusicData,
  kitNotes: ImportResult['kitNotes'] = {},
): PlaybackSetup => {
  const instrumentProfiles = [
    cinematicStrings, cinematicWoodwinds, cinematicBrass,
    cinematicPiccolo, cinematicTuba, cinematicContrabass,
    plainInstrument, ...percussionProfiles,
  ]
  const trackProfiles: PlaybackSetup['trackProfiles'] = {}
  for (const track of music.tracks) {
    const percussion = percussionFor(track, kitNotes[track.id])
    if (percussion) {
      trackProfiles[track.id] = percussion
      continue
    }
    const profile = FAMILIES.find(([pattern]) => pattern.test(track.instrumentName))?.[1]
      ?? plainInstrument
    const connected = profile !== plainInstrument
    trackProfiles[track.id] = {
      // Every Cinematic Studio part connects unslurred notes.
      interpretationProfileId: connected ? connectedIntent.id : orchestralIntent.id,
      outputs: [{ id: 'main', instrumentProfileId: profile.id }],
    }
  }
  return {
    techniques: orchestralTechniques,
    interpretationProfiles: [
      defaultIntent, orchestralIntent, connectedIntent, ...percussionIntents,
    ],
    instrumentProfiles,
    trackProfiles,
    overrides: [],
  }
}
