import { describe, expect, it } from 'vitest'

import { band } from '@/core/examples/band'
import { ScoreBuilder } from '@/core/examples/build'
import { melody } from '@/core/examples/melody'
import { piano } from '@/core/examples/piano'
import { strings } from '@/core/examples/strings'
import type { Chord, Note } from '@/core/types/MusicData'
import { DynamicType } from '@/core/types/MusicData'
import type InterpretationProfile from '@/core/types/InterpretationProfile'
import type { CurveEvent, MusicEvent, NoteEvent } from '@/core/types/MusicEvent'
import type { PlaybackSetup } from '@/core/types/PlaybackSetup'
import { expandRepeats } from '@/core/validation/repeats'
import { validate } from '@/core/validation/validate'
import { play } from '.'
import { curveKey, curveSampler, evaluateCurve, segmentValue, shape } from './curves'
import { resolveInterpretationProfile } from './interpretation'
import { renderTrack } from './renderMidi'
import type { TempoMap } from './tempo'
import { setups } from './testProfiles'

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

describe('phrasing and context', () => {
  const withRules = (extra: Partial<InterpretationProfile>): PlaybackSetup => ({
    ...setups.melody,
    interpretationProfiles: [
      ...setups.melody.interpretationProfiles,
      { id: 'test', name: 'Test', extends: 'default', noteRules: [], curveRules: [], ...extra },
    ],
    trackProfiles: {
      'mel-flute': {
        interpretationProfileId: 'test',
        outputs: [{ id: 'main', instrumentProfileId: 'wind' }],
      },
    },
  })

  it('treats unbroken runs as slurs in connected phrasing', () => {
    const { events } = play(melody(), withRules({ defaultPhrasing: 'connected' }))
    expect(notesOf(events).map(n => n.slur ?? '-')).toEqual([
      'first', 'last', // the pickup; the written slur after it is its own phrase
      'first', 'inner', 'last', // the written slur
      '-', // alone: staccato notes follow
      '-', '-', '-', '-', // staccato notes are never connected
      'first', 'inner', 'inner', 'inner', 'last', // up to the accent
      'first', 'last', // the accent starts a new run
    ])
  })

  it('switches phrasing where the page says so', () => {
    const music = melody()
    const track = music.tracks[0]!
    // Anchored to the chord of the note at a tick, wherever it is nested (tuplets too).
    const noteId = (tick: number) =>
      notesOf(play(melody(), withRules({})).events).find(n => n.startTick === tick)!.sourceIds[0]!
    const chordAt = (tick: number): string => {
      const id = noteId(tick)
      const find = (node: unknown): string | undefined => {
        if (!node || typeof node !== 'object') return undefined
        const value = node as { kind?: string, id?: string, notes?: { id: string }[] }
        if (value.kind === 'chord' && value.notes?.some(n => n.id === id))
          return value.id
        for (const child of Object.values(node)) {
          const found = find(child)
          if (found) return found
        }
        return undefined
      }
      return find(track.staves)!
    }
    const mark = (techniqueId: string, tick: number) => track.attachments.push({
      id: `mark-${tick}`, kind: 'technique', techniqueId, scope: 'until-next',
      at: { type: 'element', id: chordAt(tick) },
    })
    mark('core:connected', 6720) // "cantabile" on the run after the staccato notes
    mark('core:detached', 8960) // "maestoso" two notes before the accent
    const { events } = play(music, withRules({}))
    expect(notesOf(events).map(n => n.slur ?? '-')).toEqual([
      '-', '-', // detached by default
      'first', 'inner', 'last', // the written slur
      '-', '-', '-', '-', '-',
      'first', 'inner', 'last', // connected up to the "maestoso"
      '-', '-', '-', '-',
    ])
  })

  it('leaves unslurred notes alone by default', () => {
    const { events } = play(melody(), withRules({}))
    expect(notesOf(events).filter(n => n.slur)).toHaveLength(3)
  })

  it('lets a rule look at the note before and at the place in a slur', () => {
    const { events } = play(melody(), withRules({
      noteRules: [
        {
          id: 'after-short',
          match: { kind: 'any' },
          when: { maxPreviousNotatedQn: 0.5 },
          actions: [{ op: 'addTechnique', techniqueId: 'test:after-short' }],
        },
        {
          id: 'slur-end',
          match: { kind: 'any' },
          when: { slurPosition: 'last' },
          actions: [{ op: 'addTechnique', techniqueId: 'test:slur-end' }],
        },
      ],
    }))
    const at = (tick: number) => notesOf(events).find(n => n.startTick === tick)!.techniques
    expect(at(0)).not.toContain('test:after-short') // nothing before it
    expect(at(480)).toContain('test:after-short') // after an eighth
    expect(at(2400)).not.toContain('test:after-short') // after a dotted quarter
    expect(notesOf(events).filter(n => n.techniques.includes('test:slur-end'))
      .map(n => n.startTick)).toEqual([2880])
  })

  it('lets a rule judge length in real time', () => {
    // At 96, an eighth lasts 312 ms and a triplet eighth 208 ms.
    const fast = (when: NonNullable<InterpretationProfile['noteRules'][number]['when']>) =>
      notesOf(play(melody(), withRules({
        noteRules: [{
          id: 'fast',
          match: { kind: 'any' },
          when,
          actions: [{ op: 'addTechnique', techniqueId: 'test:fast' }],
        }],
      })).events).filter(n => n.techniques.includes('test:fast')).map(n => n.startTick)
    expect(fast({ maxNotatedMs: 250 })).toEqual([8640, 8960, 9280]) // the triplet only
    expect(fast({ maxNotatedMs: 320 })).toHaveLength(2 + 1 + 4 + 3) // and every eighth
    // The note after each triplet eighth: the other two, and the note after the triplet.
    expect(fast({ maxPreviousNotatedMs: 250 })).toEqual([8960, 9280, 9600])
    // A note exactly at the limit counts, despite rounding in the tempo map
    // (208.33... ms for a triplet eighth at 96).
    expect(fast({ maxNotatedMs: 60000 / 96 / 3 })).toEqual([8640, 8960, 9280])
  })

  it('swells into and out of long notes and slow slurs', () => {
    const swell = { minMs: 200, startLevel: 0.75, noteRatio: 0.25, slurRatio: 0.5 }
    const { events } = play(melody(), withRules({ swell }))
    const notes = notesOf(events)
    const swells = events.filter((e): e is CurveEvent =>
      e.kind === 'curve-event' && e.combine === 'scale')
    const startsOf = (list: CurveEvent[]) => list.filter(e => e.segmentIndex === 0)
      .map(e => e.startTick)

    // The written slur B–A–G gets one swell; the staccato eighths (140 ms) get none.
    const slurred = notes.filter(n => n.slur)
    expect(startsOf(swells)).toContain(slurred[0]!.startTick)
    for (const n of slurred.slice(1)) expect(startsOf(swells)).not.toContain(n.startTick)
    for (const n of notes.filter(n => n.techniques.includes('core:staccato')))
      expect(startsOf(swells)).not.toContain(n.startTick)


    // Rise over half of the slur's first note, fall over half of its last. The note after
    // it swells too, so the level stays low in between instead of jumping up.
    const [first, last] = [slurred[0]!, slurred.at(-1)!]
    const segments = (note: NoteEvent) => swells
      .filter(e => e.id.startsWith(`${note.sourceIds[0]}@1#swell`))
      .map(e => [e.startTick, e.durationTicks, e.fromValue, e.toValue])
    expect(segments(first)).toEqual([
      [first.startTick, first.durationTicks / 2, 0.75, 1],
      [last.startTick + last.durationTicks / 2, last.durationTicks / 2, 1, 0.75],
    ])
    // That single long note is followed by staccato notes: the written level comes back
    // when the first of them starts, not when the long note ends.
    const single = notes.find(n => n.startTick === 3840)!
    const staccato = notes.find(n => n.startTick > single.startTick)!
    expect(segments(single)).toEqual([
      [3840, 864 / 4, 0.75, 1],
      [3840 + 864 * 3 / 4, 864 / 4, 1, 0.75],
      [staccato.startTick, 0, 0.75, 1],
    ])

    // It scales the written level: 75 % of mp at the start, the full level in the middle.
    const curve = (tick: number) => evaluateCurve(events.filter(
      (e): e is CurveEvent => e.kind === 'curve-event'), 'dynamics', tick, 0.5)
    expect(curve(first.startTick)).toBeCloseTo(0.45 * 0.75)
    expect(curve(first.startTick + first.durationTicks)).toBeCloseTo(0.45)
    // Velocities are not touched.
    expect(first.velocity).toBeCloseTo(0.45)
  })

  it('shapes a cresc. and a dim. each its own way', () => {
    const hairpin = {
      cresc: { curveType: 'exp' as const, steepness: 3 },
      dim: { curveType: 'exp' as const, steepness: -3 },
      defaultChange: 0.2,
    }
    const shapeOf = (type: 'cresc' | 'dim') => {
      const music = melody()
      const spanner = music.tracks[0]!.trackSpanners.find(s => s.kind === 'hairpin')!
      if (spanner.kind === 'hairpin') spanner.type = type
      const segment = curvesOf(play(music, withRules({ hairpin })).events, 'dynamics')
        .find(e => e.id.startsWith(spanner.id))!
      return [segment.curveType, segment.steepness]
    }
    expect(shapeOf('cresc')).toEqual(['exp', 3])
    expect(shapeOf('dim')).toEqual(['exp', -3])
  })

  it('shapes the rise and the fall of a swell separately', () => {
    const swell = {
      minMs: 200, startLevel: 0.75, noteRatio: 0.25, slurRatio: 0.5,
      rise: { curveType: 'power' as const, steepness: -1 },
      fall: { curveType: 'power' as const, steepness: 1 },
    }
    const swells = play(melody(), withRules({ swell })).events.filter((e): e is CurveEvent =>
      e.kind === 'curve-event' && e.combine === 'scale' && e.durationTicks > 0)
    const rises = swells.filter(e => e.segmentIndex === 0)
    const falls = swells.filter(e => e.segmentIndex === 1)
    expect(rises.length).toBeGreaterThan(0)
    expect(falls.length).toBeGreaterThan(0)
    for (const e of rises) expect([e.curveType, e.steepness]).toEqual(['power', -1])
    for (const e of falls) expect([e.curveType, e.steepness]).toEqual(['power', 1])
    // Eased out: a quarter of the way in it is 44 % up (1 - 0.75²), where linear is 25 %.
    const rise = rises[0]!
    const quarter = segmentValue(rise, rise.startTick + rise.durationTicks / 4)
    expect(quarter).toBeCloseTo(0.75 + 0.25 * (1 - 0.75 ** 2))
  })

  it('leaves a swell out where the page changes the dynamic itself', () => {
    const swell = { minMs: 200, startLevel: 0.75, noteRatio: 0.25, slurRatio: 0.5 }
    const { events } = play(melody(), withRules({ swell }))
    const swells = events.filter((e): e is CurveEvent =>
      e.kind === 'curve-event' && e.combine === 'scale')
    const hairpins = curvesOf(events, 'dynamics')
      .filter(e => e.combine === undefined && e.durationTicks > 0)
    expect(hairpins.length).toBeGreaterThan(0)
    const inside = (from: number, to: number) =>
      hairpins.some(h => h.startTick < to && h.startTick + h.durationTicks > from)
    // No rise or fall lies over a hairpin...
    for (const segment of swells.filter(e => e.durationTicks > 0))
      expect(inside(segment.startTick, segment.startTick + segment.durationTicks)).toBe(false)
    // ...and some were left out because of one: a long note under the cresc. has none.
    const under = notesOf(events).filter(n => !n.slur && n.durationTicks >= 480
      && inside(n.startTick, n.startTick + n.durationTicks))
    expect(under.length).toBeGreaterThan(0)
    for (const note of under)
      expect(swells.some(e => e.segmentIndex === 0 && e.startTick === note.startTick)).toBe(false)
  })

  it('samples a curve in order the same as evaluating it tick by tick', () => {
    const swell = { minMs: 200, startLevel: 0.75, noteRatio: 0.25, slurRatio: 0.5 }
    const curves = play(melody(), withRules({ swell })).events
      .filter((e): e is CurveEvent => e.kind === 'curve-event')
    const sample = curveSampler(curves, 'dynamics', 0.5)
    for (let tick = 0; tick < 16000; tick += 97)
      expect(sample(tick)).toBeCloseTo(evaluateCurve(curves, 'dynamics', tick, 0.5))

  })

  it('lets a note take a controller over from a curve, by its length', () => {
    const setup: PlaybackSetup = {
      ...setups.melody,
      instrumentProfiles: [{
        ...setups.melody.instrumentProfiles.find(p => p.id === 'wind')!,
        techniqueMappings: [{
          matches: [{ kind: 'technique', techniqueId: 'core:staccato' }],
          cc: [{ number: 1, value: { shortMs: 100, longMs: 1000, atShort: 0, atLong: 100 } }],
        }],
      }],
    }
    const { tracks, tempo } = play(melody(), setup)
    const cc1 = tracks[0]!.messages.filter(m => m.type === 'cc' && m.data1 === 1)
    // Four staccato eighths at 96: 312.5 ms written, which is 24 on that scale.
    const taken = cc1.filter(m => m.source === 'technique-cc')
    expect(taken.map(m => m.data2)).toEqual([24, 24, 24, 24])
    expect(taken[0]!.timeMs).toBeCloseTo(tempo.tickToMs(4800) - 20) // with the keyswitch lead
    // The value is held through the gaps between them, and the curve (mp = 57) only comes
    // back at the next note that does not take it over.
    const lastEnd = tempo.tickToMs(6240 + 240)
    const between = cc1.filter(m => m.source === 'curve'
      && m.timeMs > taken[0]!.timeMs && m.timeMs < lastEnd)
    expect(between).toEqual([])
    const back = cc1.find(m => m.source === 'curve' && m.timeMs >= lastEnd)!
    expect(back.data2).toBe(57)
    const nextOn = tracks[0]!.messages.find(m => m.source === 'note' && m.type === 'note-on'
      && m.timeMs > lastEnd)!
    expect(back.timeMs).toBeLessThanOrEqual(nextOn.timeMs)
    expect(back.timeMs).toBeGreaterThan(lastEnd)
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

  it('presses a keyswitch again when only its velocity changes', () => {
    // Some libraries pick the articulation by how hard one key is pressed.
    const key = (velocity: number) => ({ keyswitch: { note: 24, velocity } })
    const setup: PlaybackSetup = {
      ...setups.melody,
      instrumentProfiles: [{
        ...setups.melody.instrumentProfiles.find(p => p.id === 'wind')!,
        techniqueMappings: [
          { matches: [], ...key(100) },
          { matches: [{ kind: 'technique', techniqueId: 'core:staccato' }], ...key(30) },
        ],
      }],
    }
    const presses = play(melody(), setup).tracks[0]!.messages
      .filter(m => m.source === 'keyswitch' && m.type === 'note-on')
      .map(m => [m.data1, m.data2])
    expect(presses).toEqual([[24, 100], [24, 30], [24, 100]])
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

  describe('ramps become one CC message per value step', () => {
    const tpq = 480
    const tempo: TempoMap = { // 60 bpm: one quarter is one second
      points: [{ tick: 0, qpm: 60 }],
      tickToMs: tick => tick * 1000 / tpq,
      msToTick: ms => ms * tpq / 1000,
    }
    const ramp = (beats: number, toValue: number) => {
      const curve: CurveEvent = {
        kind: 'curve-event', id: 'ramp', sourceIds: [], trackId: 't', pass: 1,
        segmentIndex: 0, parameter: 'dynamics', startTick: 0, durationTicks: beats * tpq,
        fromValue: 0, toValue, curveType: 'linear',
      }
      const wind = setups.melody.instrumentProfiles.find(p => p.id === 'wind')!
      return renderTrack([curve], wind, tempo, []).messages
        .filter(m => m.type === 'cc' && m.data1 === 1)
    }

    it('sends every value of a slow ramp, when it is reached', () => {
      // 0 → 127 over four seconds: one step about every 31 ms.
      const cc = ramp(4, 1)
      expect(cc.map(m => m.data2)).toEqual([...Array(128).keys()])
      // 63.5 rounds up to 64, reached halfway; found within the 1 ms check.
      const half = cc.find(m => m.data2 === 64)!
      expect(Math.abs(half.timeMs - 2000)).toBeLessThanOrEqual(1)
    })

    it('spreads values over the output range along its curve', () => {
      const wind = setups.melody.instrumentProfiles.find(p => p.id === 'wind')!
      const curved = {
        ...wind,
        curveMappings: [{
          parameter: 'dynamics' as const,
          defaultValue: 0.5,
          cc: [{ number: 1, mapping: { low: 0, high: 127, curve: { curveType: 'power' as const,
            steepness: 1 } } }],
        }],
      }
      const curve: CurveEvent = {
        kind: 'curve-event', id: 'ramp', sourceIds: [], trackId: 't', pass: 1,
        segmentIndex: 0, parameter: 'dynamics', startTick: 0, durationTicks: 4 * tpq,
        fromValue: 0, toValue: 1, curveType: 'linear',
      }
      const cc = renderTrack([curve], curved, tempo, []).messages
        .filter(m => m.type === 'cc' && m.data1 === 1)
      // Halfway up the ramp (0.5) is a quarter of the range: 0.5 squared.
      const half = cc.findLast(m => m.timeMs <= 2000)!
      expect(half.data2).toBe(Math.round(127 * 0.25))
      expect(cc.at(-1)!.data2).toBe(127)
    })

    it('keeps a fast ramp at least 10 ms apart and still ends on the target', () => {
      // 0 → 127 in a quarter of a second: a step every 2 ms, too dense to send all.
      const cc = ramp(0.25, 1)
      const gaps = cc.slice(1).map((m, i) => m.timeMs - cc[i]!.timeMs)
      expect(Math.min(...gaps.slice(0, -1))).toBeGreaterThanOrEqual(10 - 1e-6)
      expect(cc.length).toBeLessThanOrEqual(27)
      expect(cc.at(-1)!.data2).toBe(127)
      expect(cc.at(-1)!.timeMs).toBeCloseTo(250)
    })
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
