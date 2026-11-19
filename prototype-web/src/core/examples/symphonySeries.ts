/**
 * Profiles for Native Instruments Symphony Series Percussion, built from the rules in
 * `rules/symphony-series-percussion.md`, and the part names they are chosen by.
 *
 * Each instrument is its own patch, so every part gets a profile of its own. Hits are
 * played on two keys that take turns, so that repeated hits do not sound the same. A roll
 * (any tremolo) stays one note on the roll key, and CC1 carries its dynamics; hits take
 * their loudness from velocity. Mallets cannot roll, so their tremolos are written out.
 */
import type InstrumentProfile from '@/core/types/InstrumentProfile'
import type { NoteMatch, OutputRange, TechniqueMapping } from '@/core/types/InstrumentProfile'
import type InterpretationProfile from '@/core/types/InterpretationProfile'
import type { KitEntry, Track } from '@/core/types/MusicData'
import { DynamicType } from '@/core/types/MusicData'
import type { TrackOutput, TrackPlayback } from '@/core/types/PlaybackSetup'

const DYNAMICS_CC = 1

/** Keys of each sound: two white keys, used in turn. */
const HITS = [48, 50] // normal hits; long hits on the suspended cymbal and on platti
const EDGE = [52, 53] // snare edge hits; short platti (the cymbal's short hits are unused)
const ROLL = [55]
const CRESCENDO = [57] // suspended cymbal: a sample that swells into its end
/** The crescendo sample's length: it starts this long before the peak it leads to. */
const CRESCENDO_MS = 1250
/** A cymbal roll under a crescendo up to this long uses the crescendo sample instead. */
const CRESCENDO_MAX_MS = 2000

/** Keyswitches of the timpani and platti. */
const KS_HITS = { note: 36, velocity: 127 }
/** Keyswitches of the suspended cymbal: hard mallets for accents and sfp, soft otherwise. */
const KS_HARD = { note: 36, velocity: 127 }
const KS_SOFT = { note: 38, velocity: 127 }
const KS_ROLL = { note: 38, velocity: 127 }
/** Pressed at every rest: a temporary keyswitch that damps what is still ringing. */
const KS_DAMP = { note: 45, velocity: 127 }

/** Tom 4 (lowest) to Tom 1: two keys each, used in turn. */
const TOM_KEYS = { 'tom-4': [43, 45], 'tom-3': [48, 50], 'tom-2': [52, 53], 'tom-1': [55, 57] }
/** No bongos in the library: a white key from C2 for each drum or stroke, low to high. */
const WHITE_KEYS = [48, 50, 52, 53, 55, 57, 59, 60, 62, 64, 65, 67]
/** Temple blocks stand in for wood blocks: block 1 (lowest) to block 5. */
const TEMPLE_BLOCK_KEYS = [48, 50, 52, 53, 55]

/** Note length a mallet tremolo is written out in, when it is not measured. */
const MALLET_ROLL_MS = 70
/** sfp on a roll: a single stroke this long (quarter notes) comes with or before it. */
const SFP_STROKE_QN = 0.25
const TIMPANI_SFP_STROKE_QN = 0.125 // a 32nd, then the roll

/**
 * The velocity and CC1 each instrument's dynamics (0 to 1, ppp to fff) are spread over,
 * to be tuned by ear: a narrower range keeps an instrument from getting too soft or too
 * loud. Hits take it as velocity, rolls as CC1.
 */
const DEFAULT_DYNAMIC_RANGE = {
  low: 1, high: 127,
  curve: { curveType: 'power' as const, steepness: -1 },
}
const DYNAMIC_RANGES: Record<string, OutputRange> = {
  'timpani': DEFAULT_DYNAMIC_RANGE,
  'suspended-cymbal': DEFAULT_DYNAMIC_RANGE,
  'snare': DEFAULT_DYNAMIC_RANGE,
  'bass-drum': DEFAULT_DYNAMIC_RANGE,
  'platti': { low: 96, high: 127 },
  'sticks': DEFAULT_DYNAMIC_RANGE,
  'toms': DEFAULT_DYNAMIC_RANGE,
  'bongos': DEFAULT_DYNAMIC_RANGE,
  'temple-blocks': DEFAULT_DYNAMIC_RANGE,
  'mallets': DEFAULT_DYNAMIC_RANGE,
}

export const percussionTechniques = [
  { id: 'ssp:roll-crescendo' },
]

const technique = (techniqueId: string): NoteMatch => ({ kind: 'technique', techniqueId })

/** Any tremolo is a roll: one note on the roll key. */
const rolls = (mapping: Omit<TechniqueMapping, 'matches'>): TechniqueMapping[] =>
  ['core:tremolo', 'core:measured-tremolo'].map(id => ({ matches: [technique(id)], ...mapping }))

/** How the page is read for percussion: notes as long as written, no swells. */
export const percussionIntent: InterpretationProfile = {
  id: 'percussion',
  name: 'Percussion',
  extends: 'default',
  // Measured tremolos are played as written; unmeasured ones are rolls.
  expandTremolo: {},
  hairpin: {
    cresc: { curveType: 'exp', steepness: 4 },
    dim: { curveType: 'exp', steepness: -4 },
    defaultChange: 0.2,
  },
  // sfp / fp: CC1 (a roll's dynamics) at piano right away; the hit is loud by velocity,
  // and a roll gets a single loud stroke along with it.
  dynamics: [{ type: DynamicType.FP, mode: 'set', value: 0.2 }],
  noteRules: [
    {
      id: 'sfp',
      match: { kind: 'dynamic', type: DynamicType.FP },
      actions: [
        { op: 'set', for: 'velocity', value: 0.9 },
        { op: 'rollAttack', qn: SFP_STROKE_QN, together: true },
      ],
    },
    // A roll holds until the next note; a hit's length hardly matters, it rings anyway.
    {
      id: 'detached',
      match: { kind: 'any' },
      actions: [{ op: 'setRatio', for: 'duration', value: 1 }],
    },
  ],
  curveRules: [],
}

/**
 * Suspended cymbal: a roll under a short crescendo is the crescendo sample; a longer
 * one rolls with CC1 on an ease curve.
 */
export const cymbalIntent: InterpretationProfile = {
  id: 'percussion-cymbal',
  name: 'Percussion, suspended cymbal',
  extends: 'percussion',
  hairpin: {
    cresc: { curveType: 'ease' },
    dim: { curveType: 'ease' },
    defaultChange: 0.2,
  },
  noteRules: [
    ...['core:tremolo', 'core:measured-tremolo'].map(id => ({
      id: `roll-crescendo-${id}`,
      match: { kind: 'technique' as const, techniqueId: id },
      when: { hairpin: 'cresc' as const, maxNotatedMs: CRESCENDO_MAX_MS },
      actions: [{ op: 'addTechnique' as const, techniqueId: 'ssp:roll-crescendo' }],
    })),
    // sfp: hard mallets; the stroke with a roll is a long hit, ringing as long as it.
    {
      id: 'sfp',
      match: { kind: 'dynamic', type: DynamicType.FP },
      actions: [
        { op: 'set', for: 'velocity', value: 0.9 },
        { op: 'addTechnique', techniqueId: 'core:accent' },
        { op: 'rollAttack', together: true },
      ],
    },
  ],
  curveRules: [],
}

/** Timpani: an sfp roll is a short stroke first, then the roll (one patch plays either). */
export const timpaniIntent: InterpretationProfile = {
  id: 'percussion-timpani',
  name: 'Percussion, timpani',
  extends: 'percussion',
  noteRules: [{
    id: 'sfp',
    match: { kind: 'dynamic', type: DynamicType.FP },
    actions: [
      { op: 'set', for: 'velocity', value: 0.9 },
      { op: 'rollAttack', qn: TIMPANI_SFP_STROKE_QN, together: false },
    ],
  }],
  curveRules: [],
}

/** Mallets: no roll patch, so tremolos are played as repeated notes. */
export const malletIntent: InterpretationProfile = {
  id: 'percussion-mallets',
  name: 'Percussion, mallets',
  extends: 'percussion',
  expandTremolo: { unmeasuredMs: MALLET_ROLL_MS },
  noteRules: [],
  curveRules: [],
}

const symphony = (
  id: string, name: string, techniqueMappings: TechniqueMapping[],
  more: Partial<InstrumentProfile> = {},
): InstrumentProfile => {
  const range = DYNAMIC_RANGES[id.replace(/^ssp-/, '')] ?? { low: 1, high: 127 }
  return {
    id,
    name: `Symphony Series ${name}`,
    trackDelayMs: 0,
    defaultChannel: 1,
    velocity: { mapping: range },
    keyswitchLeadMs: 0,
    techniqueMappings,
    // Rolls follow CC1; hits ignore it.
    curveMappings: [{
      parameter: 'dynamics',
      defaultValue: 0.5,
      cc: [{ number: DYNAMICS_CC, mapping: range }],
    }],
    ...more,
  }
}

/** Snare drum: edge hits for accents. */
export const snareDrum = symphony('ssp-snare', 'Snare Drum', [
  { matches: [], notes: HITS },
  { matches: [technique('core:accent')], notes: EDGE },
  ...rolls({ notes: ROLL }),
])

/** Bass drum, and anything else laid out the same way (triangle, tam-tam, toms, ...). */
export const bassDrum = symphony('ssp-bass-drum', 'Bass Drum', [
  { matches: [], notes: HITS },
  ...rolls({ notes: ROLL }),
])
export const otherPercussion = { ...bassDrum, id: 'ssp-other', name: 'Symphony Series (hits)' }

/**
 * Suspended cymbal: long hits, with soft mallets by default and hard ones for accents and
 * sfp. Damped at rests (not after a note left to ring) and right after a staccato note.
 */
export const suspendedCymbal = symphony('ssp-suspended-cymbal', 'Suspended Cymbal', [
  { matches: [], notes: HITS, keyswitch: KS_SOFT },
  { matches: [technique('core:accent')], keyswitch: KS_HARD },
  ...rolls({ notes: ROLL }),
  // Last, so that it wins over the roll: the sample ends on the peak of the crescendo.
  {
    matches: [technique('ssp:roll-crescendo')],
    notes: CRESCENDO,
    durationMs: { mode: 'set', value: CRESCENDO_MS },
    alignToEnd: true,
  },
], { dampKeyswitch: { ...KS_DAMP, afterTechniques: ['core:staccato'] } })

/**
 * Platti (clash cymbals): long hits by default, short ones for staccato; damped at rests
 * and right after a staccato note.
 */
export const platti = symphony('ssp-platti', 'Platti', [
  { matches: [], notes: HITS, keyswitch: KS_HITS },
  { matches: [technique('core:staccato')], notes: EDGE },
], { dampKeyswitch: { ...KS_DAMP, afterTechniques: ['core:staccato'] } })

/**
 * Timpani: played at pitch an octave up; a keyswitch for rolls; damped at rests and right
 * after a staccato note.
 */
export const timpani = symphony('ssp-timpani', 'Timpani', [
  { matches: [], keyswitch: KS_HITS },
  ...rolls({ keyswitch: KS_ROLL }),
], { dampKeyswitch: { ...KS_DAMP, afterTechniques: ['core:staccato'] }, pitchOffset: 12 })

/** Sticks: a snare part's rim clicks and side sticks, on a patch of their own. */
export const sticks = symphony('ssp-sticks', 'Sticks', [
  { matches: [], notes: HITS },
])

/**
 * Drums with keys for each of their sounds, under the patch's own names. Which sound of
 * a score is which of these is up to the score's output (`TrackOutput.sounds`).
 */
export const toms = symphony('ssp-toms', 'Toms', [], { kit: TOM_KEYS })
export const bongos = symphony('ssp-bongos', 'Bongos (other drums)', [], {
  kit: Object.fromEntries(WHITE_KEYS.map((key, i) => [`drum-${i + 1}`, key])),
})
export const templeBlocks = symphony('ssp-temple-blocks', 'Temple Blocks', [], {
  kit: Object.fromEntries(TEMPLE_BLOCK_KEYS.map((key, i) => [`block-${i + 1}`, key])),
})

/** Mallets: plain notes, velocity for loudness. The glockenspiel plays an octave down. */
export const mallets = symphony('ssp-mallets', 'Mallets', [], { curveMappings: [] })
export const glockenspiel = {
  ...mallets, id: 'ssp-glockenspiel', name: 'Symphony Series Glockenspiel', pitchOffset: -12,
}

/** Which profile a part gets, by its name; tested in order. */
const PARTS: [RegExp, InstrumentProfile, InterpretationProfile][] = [
  [/timp/i, timpani, timpaniIntent],
  [/susp/i, suspendedCymbal, cymbalIntent],
  [/snare|side drum|tamburo/i, snareDrum, percussionIntent],
  [/bass drum|gran cassa|grosse caisse/i, bassDrum, percussionIntent],
  [/cymbal|crash|clash|piatti|platti/i, platti, percussionIntent],
  [/\btoms?\b|tom-?toms?/i, toms, percussionIntent],
  [/bongo|conga|timbale/i, bongos, percussionIntent],
  [/wood ?block|temple ?block/i, templeBlocks, percussionIntent],
  [/glock/i, glockenspiel, malletIntent],
  [/xylo|marimba|vibra|chime|tubular|bells|crotal/i, mallets, malletIntent],
]

/**
 * A part written as a whole drum kit is left at the notes the file gives its sounds
 * (General MIDI), for a drum instrument that plays those directly.
 */
const DRUM_KIT = /drum ?(set|kit)|^drums$|batterie|schlagzeug/i

/** General MIDI drums: every percussion note under the name `gm-<note>`, on channel 10. */
export const generalMidiDrums: InstrumentProfile = {
  id: 'gm-drums',
  name: 'General MIDI drums',
  trackDelayMs: 0,
  defaultChannel: 10,
  velocity: { mapping: { low: 1, high: 127 } },
  techniqueMappings: [],
  curveMappings: [],
  kit: Object.fromEntries(Array.from({ length: 128 }, (_, note) => [`gm-${note}`, note])),
}

export const percussionProfiles = [
  timpani, suspendedCymbal, snareDrum, bassDrum, otherPercussion, platti, sticks,
  toms, bongos, templeBlocks, glockenspiel, mallets, generalMidiDrums,
]
export const percussionIntents = [percussionIntent, cymbalIntent, timpaniIntent, malletIntent]

/** Sounds of a part from low to high: by staff position, then by General MIDI number. */
const lowToHigh = (entries: KitEntry[], gm: Record<string, number>) => [...entries]
  .sort((a, b) => a.displayPitch - b.displayPitch || (gm[a.sound] ?? 0) - (gm[b.sound] ?? 0))

/** Which of a patch's sounds the i-th of `count` sounds of a part is, from low to high. */
type SoundFor = (i: number, count: number) => string
const SOUND_FOR = new Map<InstrumentProfile, SoundFor>([
  // The lowest tom is Tom 4.
  [toms, i => `tom-${4 - Math.min(i, 3)}`],
  [bongos, i => `drum-${Math.min(i, WHITE_KEYS.length - 1) + 1}`],
  // Spread over the five blocks: two wood blocks are the lowest and the highest.
  [templeBlocks, (i, count) => `block-${(count <= 1
    ? 0
    : Math.min(4, count > 5 ? i : Math.round(i * 4 / (count - 1)))) + 1}`],
])

/** Patches with one sound play every sound as it, whatever it is called. */
const HIT = 'hit'

/**
 * How a part is played on Symphony Series: its outputs (DAW tracks) and the keys of its
 * sounds, or none when it is not percussion. This is a first guess made at import, from
 * the part's name and its kit; the editor keeps and corrects it. A part with unpitched
 * notes that no name matches gets the plain hits layout. `gm` is the General MIDI note the
 * file gives each kit sound: a drum kit plays those, other parts use them to order sounds
 * that share a staff line.
 */
export const percussionFor = (
  track: Track, gm: Record<string, number> = {},
): TrackPlayback | undefined => {
  const unpitched = track.kit !== undefined
  const playback = (intent: InterpretationProfile, outputs: TrackOutput[]): TrackPlayback =>
    ({ interpretationProfileId: intent.id, outputs })
  /** The part's sounds under the patch's names: low to high for several drums. */
  const soundsOf = (instrument: InstrumentProfile, entries: KitEntry[]) => {
    const soundFor = SOUND_FOR.get(instrument)
    const sorted = lowToHigh(entries, gm)
    return Object.fromEntries(sorted.map((entry, i) =>
      [entry.sound, soundFor ? soundFor(i, sorted.length) : HIT]))
  }

  // A drum kit in one part: one track, each sound on the note the file gives it.
  if (unpitched && DRUM_KIT.test(track.instrumentName)) {
    return playback(percussionIntent, [{
      id: 'main',
      instrumentProfileId: generalMidiDrums.id,
      sounds: Object.fromEntries(track.kit!.flatMap(entry =>
        gm[entry.sound] === undefined ? [] : [[entry.sound, `gm-${gm[entry.sound]}`]])),
    }])
  }

  const found = PARTS.find(([pattern]) => pattern.test(track.instrumentName))
  // "Wind chimes" written as unpitched notes are no mallet instrument.
  if (!found || (unpitched && found[2] === malletIntent)) {
    return unpitched
      ? playback(percussionIntent, [{ id: 'main', instrumentProfileId: otherPercussion.id }])
      : undefined
  }
  const [, instrument, intent] = found
  const main: TrackOutput = { id: 'main', instrumentProfileId: instrument.id }

  // Several drums in one part: each is one of the patch's drums, from low to high.
  if (SOUND_FOR.has(instrument))
    return playback(intent, [{ ...main, sounds: soundsOf(instrument, track.kit ?? []) }])

  // A snare part's rim clicks and side sticks are played on the sticks patch.
  const clicks = instrument === snareDrum
    ? (track.kit ?? []).filter(entry => /rim|stick/i.test(entry.name))
    : []
  if (clicks.length > 0) {
    return playback(intent, [main, {
      id: 'sticks',
      name: `${track.instrumentName} – Sticks`,
      instrumentProfileId: sticks.id,
      sounds: soundsOf(sticks, clicks),
    }])
  }
  return playback(intent, [main])
}
