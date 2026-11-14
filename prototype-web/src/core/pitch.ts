import type { Pitch } from './types/MusicData'
import { Step } from './types/MusicData'

export const STEPS: Step[] = [Step.C, Step.D, Step.E, Step.F, Step.G, Step.A, Step.B]
const STEP_SEMITONES = [0, 2, 4, 5, 7, 9, 11]

const stepIndex = (step: Step): number => STEPS.indexOf(step)

/** MIDI note number of a pitch (C4 = 60). */
export const midiNumber = (pitch: Pitch): number =>
  (pitch.octave + 1) * 12 + STEP_SEMITONES[stepIndex(pitch.step)]! + pitch.alter

/** Diatonic position counted in steps from C0; used for staff placement. */
export const diatonicIndex = (pitch: Pitch): number => pitch.octave * 7 + stepIndex(pitch.step)

/** Order of sharps (F C G D A E B); flats are the reverse. */
const SHARP_ORDER = [Step.F, Step.C, Step.G, Step.D, Step.A, Step.E, Step.B]

/** The alteration a key signature applies to a step, e.g. F in G major → +1. */
export const keyAlter = (accidentals: number, step: Step): number => {
  if (accidentals > 0) return SHARP_ORDER.indexOf(step) < accidentals ? 1 : 0
  if (accidentals < 0) return SHARP_ORDER.indexOf(step) >= 7 + accidentals ? -1 : 0
  return 0
}

export interface Transposition { diatonic: number, chromatic: number, octave: number }

/**
 * Moves a pitch by an interval. Transpositions in this project are written → sounding,
 * so pass `sign = -1` to get the written pitch from a sounding one.
 */
export const transposePitch = (pitch: Pitch, t: Transposition, sign: 1 | -1 = 1): Pitch => {
  const diatonic = sign * (t.diatonic + 7 * t.octave)
  const chromatic = sign * (t.chromatic + 12 * t.octave)
  const index = diatonicIndex(pitch) + diatonic
  const octave = Math.floor(index / 7)
  const step = STEPS[((index % 7) + 7) % 7]!
  const natural = midiNumber({ step, alter: 0, octave })
  return { step, octave, alter: midiNumber(pitch) + chromatic - natural }
}

/** Key signature (fifths) moved by a transposition; written key from a concert key with sign -1. */
export const transposeKey = (accidentals: number, t: Transposition, sign: 1 | -1 = 1): number => {
  let result = accidentals + sign * (7 * t.chromatic - 12 * t.diatonic)
  while (result > 7) result -= 12
  while (result < -7) result += 12
  return result
}
