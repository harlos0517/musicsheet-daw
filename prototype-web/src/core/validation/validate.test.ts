import { describe, expect, it } from 'vitest'

import { examples } from '@/core/examples'
import { band } from '@/core/examples/band'
import { melody } from '@/core/examples/melody'
import { piano } from '@/core/examples/piano'
import type MusicData from '@/core/types/MusicData'
import type { Chord, Note } from '@/core/types/MusicData'
import { Step } from '@/core/types/MusicData'
import { keyAlter, midiNumber, transposeKey, transposePitch } from './pitch'
import { durationTicks, measureTimeline } from './time'
import { validate } from './validate'

const codes = (music: MusicData) => validate(music).map(p => p.code)

/** First voice of a staff measure. */
const voiceAt = (music: MusicData, track: number, staff: number, measure: number) =>
  music.tracks[track]!.staves[staff]!.measures[measure]!.voices[0]!

describe('examples', () => {
  it.each(examples)('$name has no problems', ({ build }) => {
    expect(validate(build())).toEqual([])
  })
})

describe('validate', () => {
  it('reports duplicate ids', () => {
    const music = melody()
    music.measures[1]!.id = music.measures[0]!.id
    expect(codes(music)).toContain('duplicate-id')
  })

  it('reports voices that do not fill the measure', () => {
    const music = melody()
    voiceAt(music, 0, 0, 1).events.pop()
    expect(validate(music)).toContainEqual(expect.objectContaining({
      code: 'voice-length',
      path: 'tracks[0].staves[0].measures[1].voices[0]',
    }))
  })

  it('checks voices against a pickup bar length', () => {
    const music = melody()
    delete music.measures[0]!.actualTicks
    expect(codes(music)).toContain('voice-length')
  })

  it('reports staff measures that do not line up with the score', () => {
    const music = melody()
    music.tracks[0]!.staves[0]!.measures[2]!.measureId = 'mel-m1'
    expect(codes(music)).toContain('staff-measure-mismatch')
    music.tracks[0]!.staves[0]!.measures.pop()
    expect(codes(music)).toContain('staff-measure-count')
  })

  it('requires an initial clef, time signature and key', () => {
    const music = melody()
    music.tracks[0]!.staves[0]!.measures[0]!.clefs = []
    delete music.measures[0]!.timeSig
    delete music.measures[0]!.key
    expect(codes(music)).toEqual(expect.arrayContaining([
      'missing-initial-clef', 'missing-initial-time-sig', 'missing-initial-key',
    ]))
  })

  it('reports ties to a different pitch', () => {
    const music = melody()
    const target = (voiceAt(music, 0, 0, 4).events[0] as Chord).notes[0] as Note
    target.pitch = { step: Step.E, alter: 0, octave: 5 }
    expect(codes(music)).toContain('tie-pitch-mismatch')
  })

  it('accepts enharmonic ties with a warning', () => {
    const music = melody()
    const source = (voiceAt(music, 0, 0, 3).events[2] as Chord).notes[0] as Note
    const target = (voiceAt(music, 0, 0, 4).events[0] as Chord).notes[0] as Note
    source.pitch = { step: Step.C, alter: 1, octave: 5 }
    target.pitch = { step: Step.D, alter: -1, octave: 5 }
    const problems = validate(music)
    expect(problems.map(p => p.code)).toEqual(['tie-enharmonic'])
    expect(problems[0]!.severity).toBe('warning')
  })

  it('reports ties to a note that does not follow', () => {
    const music = melody()
    const source = (voiceAt(music, 0, 0, 1).events[0] as Chord).notes[0] as Note
    source.tie = { to: 'mel-tie-end' }
    expect(codes(music)).toEqual(expect.arrayContaining([
      'tie-not-adjacent', 'tie-duplicate-target',
    ]))
  })

  it('reports tuplets whose contents do not add up', () => {
    const music = melody()
    const tuplet = voiceAt(music, 0, 0, 3).events[0]!
    if (tuplet.kind !== 'tuplet') throw new Error('expected a tuplet')
    tuplet.actual = 4
    expect(codes(music)).toContain('tuplet-length')
  })

  it('reports anchors that point nowhere or at the wrong kind of node', () => {
    const music = melody()
    music.tracks[0]!.trackSpanners[0]!.end = { type: 'element', id: 'nope' }
    music.tracks[0]!.attachments[0]!.at = { type: 'element', id: 'mel-m1' }
    expect(codes(music)).toEqual(expect.arrayContaining(['anchor-missing', 'anchor-kind']))
  })

  it('reports reversed spanners and out-of-range ticks', () => {
    const music = melody()
    const slur = music.tracks[0]!.trackSpanners[0]!
    ;[slur.start, slur.end] = [slur.end, slur.start]
    music.tracks[0]!.trackSpanners[1]!.end = { type: 'tick', measureId: 'mel-m3', tick: 99999 }
    expect(codes(music)).toEqual(expect.arrayContaining(['spanner-reversed', 'tick-out-of-range']))
  })

  it('reports anchors on another staff for staff spanners', () => {
    const music = piano()
    const lowerChord = voiceAt(music, 0, 1, 0).events[0]!
    music.tracks[0]!.staves[0]!.staffSpanners[0]!.end = { type: 'element', id: lowerChord.id }
    expect(codes(music)).toContain('anchor-wrong-staff')
  })

  it('reports full-measure rests that share their voice', () => {
    const music = piano()
    const voice = voiceAt(music, 0, 0, 2)
    voice.events.push(voice.events[0]!)
    expect(codes(music)).toContain('full-measure-rest')
  })

  it('reports unknown kit entries and unknown staves', () => {
    const music = band()
    const chord = voiceAt(music, 1, 0, 0).events[0] as Chord
    chord.notes[0] = { id: 'x', kind: 'unpitched-note', kitEntryId: 'cowbell' }
    music.tracks[0]!.attachments[0]!.staffIds = ['band-dr-staff']
    expect(codes(music)).toEqual(expect.arrayContaining(['unknown-kit-entry', 'unknown-staff']))
  })

  it('reports duplicate voice indexes and filters naming a missing voice', () => {
    const music = piano()
    music.tracks[0]!.attachments[0]!.voiceIndex = [1]
    expect(codes(music)).toEqual([])
    music.tracks[0]!.attachments[0]!.staffIds = ['pno-lh'] // the left hand has one voice
    expect(codes(music)).toContain('unknown-voice')
    music.tracks[0]!.staves[0]!.measures[0]!.voices[1]!.index = 0
    expect(codes(music)).toContain('duplicate-voice-index')
  })

  it('reports jumps to the wrong kind of marker', () => {
    const music = band()
    const jump = music.measures[5]!.jumps[0]!
    jump.until = 'band-coda'
    jump.then = 'band-m1'
    expect(validate(music).filter(p => p.code === 'jump-target')).toHaveLength(2)
  })

  it('reports durations that do not fit the tick resolution', () => {
    const music = melody()
    music.tpq = 4
    expect(codes(music)).toContain('non-integer-ticks')
  })
})

describe('time', () => {
  it('computes durations', () => {
    expect(durationTicks({ type: 0, dot: 0 }, 960)).toBe(960)
    expect(durationTicks({ type: 1, dot: 1 }, 960)).toBe(2880)
    expect(durationTicks({ type: -1, dot: 2 }, 960)).toBe(840)
  })

  it('places measures, including pickups', () => {
    const timeline = measureTimeline(melody())
    expect(timeline.map(m => [m.startTick, m.lengthTicks])).toEqual([
      [0, 960], [960, 3840], [4800, 3840], [8640, 3840], [12480, 2880],
    ])
  })
})

describe('pitch', () => {
  it('computes MIDI numbers', () => {
    expect(midiNumber({ step: Step.C, alter: 0, octave: 4 })).toBe(60)
    expect(midiNumber({ step: Step.B, alter: 1, octave: 3 })).toBe(60)
  })

  it('applies key signatures', () => {
    expect(keyAlter(1, Step.F)).toBe(1)
    expect(keyAlter(1, Step.C)).toBe(0)
    expect(keyAlter(-2, Step.E)).toBe(-1)
    expect(keyAlter(-2, Step.A)).toBe(0)
  })

  it('transposes for a B♭ clarinet (written → sounding is down a major second)', () => {
    const clarinet = { diatonic: -1, chromatic: -2, octave: 0 }
    expect(transposePitch({ step: Step.B, alter: -1, octave: 4 }, clarinet, -1))
      .toEqual({ step: Step.C, alter: 0, octave: 5 })
    expect(transposePitch({ step: Step.D, alter: 0, octave: 5 }, clarinet))
      .toEqual({ step: Step.C, alter: 0, octave: 5 })
    expect(transposeKey(0, clarinet, -1)).toBe(2)
    expect(transposeKey(-1, clarinet, -1)).toBe(1)
  })
})
