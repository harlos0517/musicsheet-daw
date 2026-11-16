import { describe, expect, it } from 'vitest'

import { band } from '@/core/examples/band'
import { ScoreBuilder } from '@/core/examples/build'
import { melody } from '@/core/examples/melody'
import { piano } from '@/core/examples/piano'
import { setups } from '@/core/examples/profiles'
import { strings } from '@/core/examples/strings'
import type { Chord, Note } from '@/core/types/MusicData'
import { DynamicType } from '@/core/types/MusicData'
import type { CurveEvent, MusicEvent, NoteEvent } from '@/core/types/MusicEvent'
import type { PlaybackSetup } from '@/core/types/PlaybackSetup'
import { expandRepeats } from '@/core/validation/repeats'
import { validate } from '@/core/validation/validate'
import { play } from '.'
import { curveKey, shape } from './curves'
import { resolveInterpretationProfile } from './interpretation'

const notesOf = (events: MusicEvent[], trackId?: string) => events.filter(
  (e): e is NoteEvent => e.kind === 'note-event' && (!trackId || e.trackId === trackId),
)
const curvesOf = (events: MusicEvent[], key: string) => events.filter(
  (e): e is CurveEvent => e.kind === 'curve-event' && curveKey(e.parameter) === key,
)
const noteOf = (events: MusicEvent[], sourceId: string, pass = 1) => {
  const note = notesOf(events).find(e => e.sourceIds.includes(sourceId) && e.pass === pass)
  if (!note) throw new Error(`no note event for ${sourceId}`)
  return note
}
/** The event of the first note of the chord with this id. */
const chordNote = (music: ReturnType<typeof melody>, events: MusicEvent[], chordId: string) => {
  for (const track of music.tracks) {
    for (const staff of track.staves) {
      for (const measure of staff.measures) {
        for (const voice of measure.voices) {
          for (const event of voice.events) {
            if (event.kind === 'chord' && event.id === chordId)
              return noteOf(events, event.notes[0]!.id)

          }
        }
      }
    }
  }
  throw new Error(`no chord ${chordId}`)
}

/** Minimal reader for the files `writeMidiFile` produces (no running status). */
const readMidi = (bytes: Uint8Array) => {
  let at = 0
  const uint = (length: number) => {
    let value = 0
    for (let i = 0; i < length; i += 1) value = value * 256 + bytes[at++]!
    return value
  }
  const text = (length: number) => String.fromCharCode(...bytes.slice(at, at += length))
  const vlq = () => {
    let value = 0
    for (;;) {
      const byte = bytes[at++]!
      value = value * 128 + (byte & 0x7f)
      if (byte < 0x80) return value
    }
  }
  const header = { id: text(4), length: uint(4), format: uint(2), tracks: uint(2), tpq: uint(2) }
  const tracks: { tick: number, status: number, data: number[] }[][] = []
  for (let t = 0; t < header.tracks; t += 1) {
    expect(text(4)).toBe('MTrk')
    const end = uint(4) + at
    const events: { tick: number, status: number, data: number[] }[] = []
    let tick = 0
    while (at < end) {
      tick += vlq()
      const status = bytes[at++]!
      if (status === 0xff) {
        const type = bytes[at++]!
        const length = vlq()
        events.push({ tick, status, data: [type, ...bytes.slice(at, at += length)] })
      } else
        events.push({ tick, status, data: [bytes[at++]!, bytes[at++]!] })

    }
    tracks.push(events)
  }
  return { header, tracks, consumed: at }
}

describe('strings example', () => {
  it('is a valid score', () => {
    expect(validate(strings())).toEqual([])
  })
})

describe('expandRepeats', () => {
  it('plays repeats, voltas and D.S. al Coda in order', () => {
    const order = expandRepeats(band()).map(m => `${m.info.measure.id.slice(5)}:${m.pass}`)
    expect(order).toEqual([
      'm1:1', 'm2:1', 'm1:2', 'm3:1', 'm4:1', 'm5:1', 'm6:1', 'm4:2', 'm5:2', 'm7:1',
    ])
  })

  it('leaves a score without repeats as written', () => {
    expect(expandRepeats(melody()).map(m => m.startTick)).toEqual([0, 960, 4800, 8640, 12480])
  })
})

describe('interpretation profiles', () => {
  it('lets a child replace, disable and add to its parent', () => {
    const { interpretationProfiles: intentProfiles } = setups.piano
    const base = resolveInterpretationProfile(intentProfiles, 'default')
    const pianoProfile = resolveInterpretationProfile(intentProfiles, 'piano')
    expect(pianoProfile.noteRules.map(r => r.id)).toEqual(
      base.noteRules.map(r => r.id).filter(id => !id.startsWith('slur')),
    )
    expect(pianoProfile.noteRules[0]).toMatchObject({
      id: 'detached', actions: [{ op: 'setRatio', value: 1 }],
    })
    expect(pianoProfile.dynamics).toEqual(base.dynamics)

    const stringsProfile = resolveInterpretationProfile(intentProfiles, 'strings')
    expect(stringsProfile.dynamics?.find(d => d.type === 'sfz')?.value).toBe(0.4)
    expect(stringsProfile.dynamics).toHaveLength(base.dynamics!.length)
    expect(stringsProfile.curveRules.map(r => r.id)).toContain('molto-vibrato')
  })
})

describe('note events', () => {
  const music = melody()
  const { events } = play(music, setups.melody)

  it('gives every note an event with a stable id', () => {
    const notes = notesOf(events)
    expect(notes).toHaveLength(17) // 18 written notes, two of them tied together
    expect(new Set(notes.map(n => n.id)).size).toBe(17)
    expect(notes.every(n => n.id.endsWith('@1'))).toBe(true)
  })

  it('applies articulation rules', () => {
    const staccato = notesOf(events).filter(n => n.techniques.includes('core:staccato'))
    expect(staccato).toHaveLength(4)
    expect(staccato.every(n => n.notatedTicks === 480 && n.durationTicks === 240)).toBe(true)

    const accent = notesOf(events).find(n => n.techniques.includes('core:accent'))!
    const plain = notesOf(events).find(n => n.startTick === 9280)!
    expect(accent.velocity).toBeGreaterThan(plain.velocity + 0.15)
  })

  it('merges tied notes into one event', () => {
    const tied = noteOf(events, 'mel-tie-end')
    expect(tied.sourceIds).toHaveLength(2)
    expect(tied.startTick).toBe(10560)
    expect(tied.notatedTicks).toBe(3840)
  })

  it('marks slur positions and connects slurred notes', () => {
    const slurred = notesOf(events).filter(n => n.slur)
    expect(slurred.map(n => n.slur)).toEqual(['first', 'inner', 'last'])
    expect(slurred.map(n => n.durationTicks)).toEqual([1440, 480, 864])
    expect(slurred.every(n => n.techniques.includes('core:legato'))).toBe(true)
  })

  it('takes the time of a grace note before the beat from the note before it', () => {
    const { events } = play(piano(), setups.piano)
    const rightHand = notesOf(events).filter(n => n.staffId === 'pno-rh' && n.voiceIndex === 0)
    const grace = rightHand.find(n => n.pitch === 79)!
    const before = rightHand.find(n => n.startTick === 1920)!
    expect(grace).toMatchObject({ startTick: 2880 - 120, durationTicks: 120 })
    expect(before.startTick + before.durationTicks).toBe(grace.startTick)
    // Other voices are left alone: the left hand still holds its chord to the barline.
    const leftHand = notesOf(events).filter(n => n.staffId === 'pno-lh' && n.startTick === 0)
    expect(leftHand.every(n => n.durationTicks === 2880)).toBe(true)
  })

  it('reads velocity from the dynamics curve', () => {
    const first = notesOf(events)[0]!
    expect(first.velocity).toBeCloseTo(0.45) // mp
    const inHairpin = notesOf(events).filter(n => n.startTick >= 8640 && n.startTick < 9600)
    expect(inHairpin.map(n => n.velocity)).toEqual([...inHairpin.map(n => n.velocity)].sort())
    expect(inHairpin.at(-1)!.velocity).toBeGreaterThan(0.45)
  })
})

describe('curve events', () => {
  it('turns dynamics and hairpins into segments that aim at the next dynamic', () => {
    const { events } = play(melody(), setups.melody)
    expect(curvesOf(events, 'dynamics').map(c => [c.id, c.startTick, c.durationTicks, c.toValue]))
      .toEqual([
        ['mel-dyn-mp@1#0', 0, 0, 0.45],
        ['mel-cresc@1#0', 8640, 3840, 0.7],
        ['mel-dyn-f@1#0', 12480, 0, 0.7],
      ])
  })

  it('makes a pedal a press and a release at the end', () => {
    const { events } = play(piano(), setups.piano)
    expect(curvesOf(events, 'sustain')).toMatchObject([
      { segmentIndex: 0, startTick: 0, durationTicks: 0, toValue: 1 },
      {
        segmentIndex: 1,
        startTick: 0,
        durationTicks: 5760,
        fromValue: 1,
        toValue: 0,
        curveType: 'hold',
      },
    ])
  })

  it('handles fp and sfz as a jump plus a return', () => {
    const music = strings()
    const { events } = play(music, setups.strings)
    const dynamics = curvesOf(events, 'dynamics')
    expect(dynamics.filter(c => c.sourceIds[0] === 'vln-dyn-fp')).toMatchObject([
      { durationTicks: 0, toValue: 0.7 },
      { durationTicks: 240, fromValue: 0.7, toValue: 0.35, curveType: 'exp' },
    ])
    // sfz in the strings profile: 0.4 added on top of the level (0.35), fading back out.
    const sfz = dynamics.filter(c => c.sourceIds[0] === 'vln-dyn-sfz')
    expect(sfz).toMatchObject([
      { combine: 'add', durationTicks: 0, toValue: 0.4 },
      { combine: 'add', durationTicks: 480, fromValue: 0.4, toValue: 0 },
    ])
    expect(chordNote(music, events, 'vln-sfz').velocity).toBeCloseTo(0.75)
  })

  it('keeps a hairpin running under an accent', () => {
    const music = melody()
    const accented = music.tracks[0]!.staves[0]!.measures[3]!.voices[0]!.events[1]!
    music.tracks[0]!.attachments.push({
      id: 'mel-sfz',
      kind: 'dynamic',
      type: DynamicType.SFZ,
      at: { type: 'element', id: accented.id },
    })
    const { events } = play(music, setups.melody)
    // The hairpin goes 0.45 → 0.7 over bar 3; the sfz sits a quarter of the way in.
    const accent = notesOf(events).find(n => n.startTick === 9600)!
    expect(accent.velocity).toBeCloseTo(0.45 + 0.25 / 4 + 0.3 + 0.15)
    // Half way, the accent has faded and the crescendo is where it would have been.
    const after = notesOf(events).find(n => n.startTick === 10560)!
    expect(after.velocity).toBeCloseTo(0.45 + 0.25 / 2)
  })

  it('runs a custom line through a curve rule', () => {
    const { events } = play(strings(), setups.strings)
    expect(curvesOf(events, 'custom:vibrato').map(c => [c.startTick, c.durationTicks, c.toValue]))
      .toEqual([[7680, 0, 0.3], [7680, 1920, 1], [9600, 1920, 0.3]])
  })

  it('ends a span at the next span\'s start value when it has no end value', () => {
    const open: PlaybackSetup = {
      ...setups.strings,
      interpretationProfiles: setups.strings.interpretationProfiles.map(profile => ({
        ...profile,
        curveRules: profile.curveRules.map(({ endValue: _, ...rule }) => rule),
      })),
    }
    const peak = (music: ReturnType<typeof strings>) => Math.max(
      ...curvesOf(play(music, open).events, 'custom:vibrato')
        .filter(c => c.sourceIds[0] === 'vln-vib').map(c => c.toValue),
    )
    // Nothing follows: the default is 1.
    expect(peak(strings())).toBe(1)

    // A second line follows; its rule starts at 0.3, so the first one aims there.
    const music = strings()
    music.tracks[0]!.trackSpanners.push({
      id: 'vln-vib2',
      kind: 'custom-spanner',
      techniqueId: 'user:molto-vibrato',
      start: { type: 'tick', measureId: 'vln-m4', tick: 0 },
      end: { type: 'tick', measureId: 'vln-m4', tick: 3840 },
    })
    expect(peak(music)).toBeCloseTo(0.3)
  })
})

describe('curve shapes', () => {
  it('always run from 0 to 1, and a = 0 is a straight line', () => {
    for (const type of ['linear', 'exp', 'power', 'ease'] as const) {
      for (const a of [undefined, -2, 0, 2]) {
        expect(shape(type, a, 0)).toBeCloseTo(0)
        expect(shape(type, a, 1)).toBeCloseTo(1)
      }
      expect(shape(type, 0, 0.3)).toBeCloseTo(0.3)
    }
    expect(shape('hold', undefined, 0.99)).toBe(0)
  })

  it('start slowly for positive a and fast for negative a', () => {
    expect(shape('exp', 3, 0.5)).toBeLessThan(0.5)
    expect(shape('exp', -3, 0.5)).toBeGreaterThan(0.5)
    expect(shape('power', 1, 0.5)).toBeCloseTo(0.25)
    expect(shape('power', -1, 0.5)).toBeCloseTo(0.75)
    // ease is symmetric around the middle
    expect(shape('ease', 1, 0.5)).toBeCloseTo(0.5)
    expect(shape('ease', 1, 0.25)).toBeCloseTo(0.125)
    expect(shape('ease', 1, 0.75)).toBeCloseTo(0.875)
    expect(shape('ease', -1, 0.25)).toBeCloseTo(0.375)
  })
})

describe('techniques', () => {
  it('keeps a marking until the next one of its group', () => {
    const music = strings()
    const { events } = play(music, setups.strings)
    const byBar = (from: number) => notesOf(events).filter(
      n => n.startTick >= from && n.startTick < from + 3840,
    )
    expect(byBar(0).every(n => n.techniques.length === 0)).toBe(true)
    expect(byBar(3840).every(n => n.techniques.includes('core:pizz'))).toBe(true)
    expect(byBar(7680).every(
      n => n.techniques.includes('core:arco') && !n.techniques.includes('core:pizz'),
    )).toBe(true)
    expect(chordNote(music, events, 'vln-last').techniques).toContain('user:sul-tasto')
    expect(chordNote(music, events, 'vln-arco').techniques).toContain('user:molto-vibrato')
  })

  it('uses the written state after a jump back', () => {
    const { events } = play(band(), setups.band)
    const clarinet = notesOf(events, 'band-cl')
    // m4 starts the legato marking and is played twice (D.S.); m1–m3 come before it.
    expect(clarinet.filter(n => n.startTick < 15360).every(n => n.techniques.length === 0))
      .toBe(true)
    expect(clarinet.filter(n => n.pass === 2 && n.startTick >= 26880)
      .every(n => n.techniques.includes('core:legato'))).toBe(true)
  })
})

describe('overrides', () => {
  const { events } = play(strings(), setups.strings)

  it('patches a note event', () => {
    const last = noteOf(events, 'vln-last-note')
    expect(last.startTick).toBe(11520 + 60)
    expect(last.startTick + last.durationTicks).toBe(11520 + 3840 * 0.9) // the end stays
    expect(last.velocity).toBeCloseTo(0.35 * 0.9)
  })

  it('patches one segment of a curve', () => {
    const dim = curvesOf(events, 'dynamics').find(c => c.sourceIds[0] === 'vln-dim')!
    expect(dim).toMatchObject({ segmentIndex: 0, fromValue: 0.35, toValue: 0.1 })
  })
})

/** Band score with the clarinet part of some bars replaced. */
const bandWith = (bars: Record<number, string>) => {
  const music = band()
  const b = new ScoreBuilder('x')
  for (const [bar, text] of Object.entries(bars))
    music.tracks[0]!.staves[0]!.measures[Number(bar) - 1]!.voices[0]!.events = b.seq(text)

  const first = (bar: number) => {
    const chord = music.tracks[0]!.staves[0]!.measures[bar - 1]!.voices[0]!.events[0] as Chord
    return chord.notes[0] as Note
  }
  const last = (bar: number) => {
    const chord = music.tracks[0]!.staves[0]!.measures[bar - 1]!.voices[0]!.events.at(-1) as Chord
    return chord.notes[0] as Note
  }
  return { music, first, last }
}

describe('repeats and jumps', () => {
  // Played order of the band score: m1 m2 | m1 m3 m4 m5 m6 | D.S. m4 m5 | coda m7.
  const bar = (n: number) => (n - 1) * 3840

  it('restores the written dynamic when playback jumps', () => {
    const music = band()
    const attachments = music.tracks[0]!.attachments
    const at = (measureId: string, tick: number) => ({ type: 'tick' as const, measureId, tick })
    // p on beat 2 of bar 1, a crescendo through the first ending,
    // and a crescendo from bar 3 to the end of bar 5 that the D.S. lands in the middle of.
    attachments.push({ id: 'x-p', kind: 'dynamic', type: DynamicType.P, at: at('band-m1', 960) })
    music.tracks[0]!.trackSpanners.push(
      {
        id: 'x-cresc1', kind: 'hairpin', type: 'cresc',
        start: at('band-m2', 0), end: at('band-m2', 3840),
      },
      {
        id: 'x-cresc2', kind: 'hairpin', type: 'cresc',
        start: at('band-m3', 0), end: at('band-m5', 3840),
      },
    )
    const { events } = play(music, setups.band)
    const clarinet = notesOf(events, 'band-cl')
    const velocityAt = (tick: number) => clarinet.find(n => n.startTick === tick)!.velocity

    // Bar 1 the second time: back to the level before the p, then p again on beat 2.
    expect(velocityAt(bar(3))).toBeCloseTo(velocityAt(0))
    expect(velocityAt(bar(3) + 960)).toBeCloseTo(0.35)
    // Bar 4 after the D.S. sounds as it did the first time: a third into the crescendo.
    expect(velocityAt(bar(8))).toBeCloseTo(velocityAt(bar(5)))
    expect(velocityAt(bar(8))).toBeGreaterThan(0.35)
    expect(velocityAt(bar(9))).toBeCloseTo(velocityAt(bar(6)))

    const reentry = curvesOf(events, 'dynamics').find(c => c.startTick === bar(8))!
    expect(reentry).toMatchObject({ sourceIds: ['x-cresc2'], pass: 2, durationTicks: 2 * 3840 })
    expect(reentry.fromValue).toBeCloseTo(velocityAt(bar(8)))
    const ids = events.map(e => e.id)
    expect(new Set(ids).size).toBe(ids.length)
  })

  it('ends a tied note at the barline when its continuation is skipped', () => {
    // Bar 1 ends on a C5 tied into the first ending, which is skipped the second time.
    const { music, first, last } = bandWith({ 2: '2:C5 2:r' })
    last(1).tie = [{ to: first(2).id }]
    const { events } = play(music, setups.band)
    const tied = notesOf(events, 'band-cl').filter(n => n.sourceIds[0] === last(1).id)
    expect(tied.map(n => [n.pass, n.notatedTicks, n.sourceIds.length]))
      .toEqual([[1, 960 + 1920, 2], [2, 960, 1]])
    expect(notesOf(events, 'band-cl').some(n => n.sourceIds[0] === first(2).id)).toBe(false)
  })

  it('follows a tie into whichever ending is played', () => {
    // The last C5 of bar 1 is tied into both endings (bars 2 and 3).
    const { music, first, last } = bandWith({ 2: '2:C5 2:r', 3: '2:C5 2:r' })
    last(1).tie = [{ to: first(2).id }, { to: first(3).id }]
    expect(validate(music)).toEqual([])
    const { events } = play(music, setups.band)
    const clarinet = notesOf(events, 'band-cl')
    expect(clarinet.filter(n => n.sourceIds[0] === last(1).id).map(n => n.sourceIds))
      .toEqual([[last(1).id, first(2).id], [last(1).id, first(3).id]])
    expect(clarinet.some(n => [first(2).id, first(3).id].includes(n.sourceIds[0]!))).toBe(false)
  })

  it('sounds a tied-to note as a new note when its tie was not just played', () => {
    // Bar 3 is a whole note tied into bar 4, where the D.S. lands.
    const { music, first } = bandWith({ 3: '1:A4' })
    first(3).tie = [{ to: first(4).id }]
    const { events } = play(music, setups.band)
    const clarinet = notesOf(events, 'band-cl')
    expect(clarinet.find(n => n.sourceIds[0] === first(3).id))
      .toMatchObject({ startTick: bar(4), notatedTicks: 3840 + 480 })
    expect(clarinet.filter(n => n.sourceIds[0] === first(4).id))
      .toMatchObject([{ startTick: bar(8), pass: 2, notatedTicks: 480 }])
  })

  it('uses the tempo written at the place it jumps to', () => {
    const music = band()
    const b = new ScoreBuilder('x')
    music.measures[5]!.tempos.push(b.tempo('4', 60)) // bar 6, just before the D.S.
    const { tempo } = play(music, setups.band)
    const barMs = (n: number) => tempo.tickToMs(bar(n + 1)) - tempo.tickToMs(bar(n))
    expect(barMs(6)).toBeCloseTo(2000) // bar 5, at 120
    expect(barMs(7)).toBeCloseTo(4000) // bar 6, at 60
    expect(barMs(8)).toBeCloseTo(2000) // bar 4 again: 120, as written there
  })
})

describe('tempo map', () => {
  it('converts ticks to time and back', () => {
    const { tempo } = play(melody(), setups.melody)
    expect(tempo.tickToMs(960)).toBeCloseTo(625) // quarter = 96
    expect(tempo.msToTick(625)).toBeCloseTo(960)
  })

  it('slows down through a rit.', () => {
    const { tempo, played } = play(band(), setups.band)
    const coda = played.at(-1)!.startTick
    const at = (n: number) => tempo.tickToMs(coda + n * 960)
    const beat = (n: number) => at(n + 1) - at(n)
    expect(tempo.tickToMs(coda)).toBeCloseTo(9 * 2000) // nine bars at 120
    expect(beat(0)).toBeGreaterThan(500)
    expect(beat(3)).toBeGreaterThan(beat(0))
    expect(tempo.points.at(-1)!.qpm).toBe(90)
    expect(tempo.msToTick(tempo.tickToMs(coda + 1234))).toBeCloseTo(coda + 1234)
  })
})

describe('MIDI output', () => {
  it('maps kit sounds and channels', () => {
    const { tracks, warnings } = play(band(), setups.band)
    expect(warnings).toEqual([])
    const drums = tracks.find(t => t.trackId === 'band-dr')!.messages
    expect(new Set(drums.map(m => m.channel))).toEqual(new Set([10]))
    expect(new Set(drums.map(m => m.data1))).toEqual(new Set([36, 38, 42]))
    expect(drums.filter(m => m.type === 'note-on')).toHaveLength(110)
  })

  it('sends a keyswitch only when the articulation changes', () => {
    const { tracks } = play(melody(), setups.melody)
    const presses = tracks[0]!.messages
      .filter(m => m.source === 'keyswitch' && m.type === 'note-on')
      .map(m => m.data1)
    expect(presses).toEqual([24, 26, 24, 25, 24])
  })

  it('sends curves as controllers', () => {
    const pedal = play(piano(), setups.piano).tracks[0]!.messages
      .filter(m => m.type === 'cc' && m.data1 === 64)
    expect(pedal.map(m => [Math.round(m.timeMs), m.data2])).toEqual([[0, 127], [5000, 0]])

    const cc1 = play(melody(), setups.melody).tracks[0]!.messages
      .filter(m => m.type === 'cc' && m.data1 === 1).map(m => m.data2)
    expect(cc1[0]).toBe(57) // mp
    expect(cc1.at(-1)).toBe(89) // f
    expect(cc1).toEqual([...cc1].sort((a, b) => a - b))
  })

  it('merges technique mappings per channel', () => {
    const music = strings()
    const { events, tracks, tempo } = play(music, setups.strings)
    const messages = tracks[0]!.messages
    const on = (note: NoteEvent) => messages.filter(m =>
      m.source === 'note' && m.type === 'note-on'
      && Math.abs(m.timeMs - tempo.tickToMs(note.startTick)) < 1)

    expect(on(chordNote(music, events, 'vln-fp')).map(m => m.channel)).toEqual([4])
    expect(on(chordNote(music, events, 'vln-pizz')).map(m => m.channel)).toEqual([5])

    // pizz. replaces the channel, the accent layer is added and has its own velocity.
    const accent = on(chordNote(music, events, 'vln-accent'))
    expect(accent.map(m => m.channel)).toEqual([5, 6])
    expect(accent[1]!.data2).toBeLessThan(accent[0]!.data2)

    // "sul tasto" is unknown to this instrument and falls back to its soft-bow mapping.
    // The base mapping resets that controller on every channel a note sounds on.
    const bow = messages.filter(m => m.source === 'technique-cc')
    expect(bow.filter(m => m.channel === 4).map(m => [m.data1, m.data2]))
      .toEqual([[20, 0], [20, 100]])
    expect(bow.filter(m => m.channel !== 4).map(m => m.data2)).toEqual([0, 0])

    // The vibrato curve drives CC21 and switches patch above 0.6, on channel 4 only.
    const vibrato = messages.filter(m => m.type === 'cc' && m.data1 === 21)
    expect(new Set(vibrato.map(m => m.channel))).toEqual(new Set([4]))
    expect(Math.max(...vibrato.map(m => m.data2))).toBe(127)
    expect(messages.filter(m => m.source === 'keyswitch' && m.type === 'note-on')
      .map(m => m.data1)).toEqual([12, 13, 12])
  })
})

describe('MIDI file', () => {
  const cases = { melody, piano, band, strings }

  it('gives a pickup bar and a short last bar their own time signature', () => {
    const file = readMidi(play(melody(), setups.melody).file)
    const signatures = file.tracks[0]!
      .filter(e => e.status === 0xff && e.data[0] === 0x58)
      .map(e => [e.tick, `${e.data[1]}/${2 ** e.data[2]!}`])
    // Two eighths of pickup, three full bars, then the three beats that complete it.
    expect(signatures).toEqual([[0, '1/4'], [960, '4/4'], [12480, '3/4']])
  })

  it.each(Object.keys(cases) as (keyof typeof cases)[])('%s reads back', name => {
    const music = cases[name]()
    const result = play(music, setups[name])
    const file = readMidi(result.file)

    expect(file.header).toEqual({
      id: 'MThd', length: 6, format: 1, tracks: music.tracks.length + 1, tpq: music.tpq,
    })
    expect(file.consumed).toBe(result.file.length)
    for (const events of file.tracks) expect(events.at(-1)!.data[0]).toBe(0x2f)

    result.tracks.forEach((track, i) => {
      const written = file.tracks[i + 1]!.filter(e => e.status !== 0xff)
      expect(written).toHaveLength(track.messages.length)
      const on = written.filter(e => (e.status & 0xf0) === 0x90)
      const off = written.filter(e => (e.status & 0xf0) === 0x80)
      expect(on).toHaveLength(off.length)
    })

    const tempos = file.tracks[0]!.filter(e => e.status === 0xff && e.data[0] === 0x51)
    expect(tempos).toHaveLength(result.tempo.points.length)
    const lastTick = Math.max(...file.tracks.flat().map(e => e.tick))
    const scoreEnd = result.played.at(-1)!.startTick + result.played.at(-1)!.info.lengthTicks
    expect(lastTick).toBeLessThanOrEqual(scoreEnd)
  })
})
