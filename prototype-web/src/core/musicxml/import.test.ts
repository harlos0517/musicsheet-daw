import { describe, expect, it } from 'vitest'

import { cinematicSetup } from '@/core/examples/cinematicStudio'
import { bassDrum, timpani as timpaniProfile } from '@/core/examples/symphonySeries'
import { play } from '@/core/playback'
import { shape } from '@/core/playback/curves'
import type { OutputRange } from '@/core/types/InstrumentProfile'
import type { Chord, Note, Rest, Tuplet, UnpitchedNote } from '@/core/types/MusicData'
import { midiNumber } from '@/core/validation/pitch'
import { validate } from '@/core/validation/validate'
import { importMusicXml } from './import'
import { child, childrenOf, parseXml } from './xml'

const note = (step: string, octave: number, duration: number, type: string, extra = '') => `
  <note><pitch><step>${step}</step><octave>${octave}</octave></pitch>
    <duration>${duration}</duration><voice>1</voice><type>${type}</type>${extra}</note>`

const triplet = (mark: string) => `
  <time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes>
  </time-modification>
  ${mark ? `<notations><tuplet type="${mark}"/></notations>` : ''}`

/** Clarinet in B♭ and a snare drum, two bars of 4/4, 12 divisions to the quarter. */
const SCORE = `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 4.0 Partwise//EN" "x.dtd">
<score-partwise version="4.0">
  <work><work-title>Import &amp; Test</work-title></work>
  <identification><creator type="composer">Nobody</creator></identification>
  <defaults><scaling><millimeters>7</millimeters></scaling></defaults>
  <part-list>
    <score-part id="P1"><part-name>Clarinet in Bb</part-name>
      <part-abbreviation>Cl.</part-abbreviation></score-part>
    <score-part id="P2"><part-name>Snare Drum</part-name>
      <score-instrument id="P2-I39"><instrument-name>Snare</instrument-name></score-instrument>
      <midi-instrument id="P2-I39"><midi-unpitched>39</midi-unpitched></midi-instrument>
    </score-part>
  </part-list>
  <part id="P1">
    <measure number="1">
      <attributes><divisions>12</divisions><key><fifths>2</fifths></key>
        <time><beats>4</beats><beat-type>4</beat-type></time>
        <clef><sign>G</sign><line>2</line></clef>
        <transpose><diatonic>-1</diatonic><chromatic>-2</chromatic></transpose>
      </attributes>
      <direction><direction-type><metronome><beat-unit>quarter</beat-unit>
        <per-minute>120</per-minute></metronome></direction-type><sound tempo="120"/></direction>
      <direction><direction-type><dynamics><p/></dynamics></direction-type></direction>
      <direction><direction-type><wedge type="crescendo" number="1"/></direction-type></direction>
      ${note('D', 5, 12, 'quarter', '<notations><slur type="start" number="1"/></notations>')}
      ${note('E', 5, 12, 'quarter', `<notations><slur type="stop" number="1"/>
        <articulations><staccato/><strong-accent/></articulations></notations>`)}
      ${note('D', 5, 4, 'eighth', triplet('start'))}
      ${note('E', 5, 4, 'eighth', triplet(''))}
      ${note('F', 5, 4, 'eighth', triplet('stop'))}
      <direction><direction-type><wedge type="stop" number="1"/></direction-type></direction>
      <direction><direction-type><dynamics><f/></dynamics></direction-type></direction>
      ${note('G', 5, 12, 'quarter', `<tie type="start"/>
        <notations><tied type="start"/></notations>`)}
    </measure>
    <measure number="2">
      ${note('G', 5, 24, 'half', `<tie type="stop"/><notations><tied type="stop"/>
        <ornaments><tremolo type="single">3</tremolo></ornaments><fermata/></notations>`)}
      <note><grace slash="yes"/><pitch><step>A</step><octave>5</octave></pitch>
        <voice>1</voice><type>eighth</type></note>
      ${note('B', 5, 12, 'quarter')}
      <note><chord/><pitch><step>D</step><octave>6</octave></pitch>
        <duration>12</duration><voice>1</voice><type>quarter</type></note>
      <note><rest/><duration>12</duration><voice>1</voice><type>quarter</type></note>
      <backup><duration>48</duration></backup>
      <forward><duration>24</duration></forward>
      <note><pitch><step>D</step><octave>4</octave></pitch>
        <duration>12</duration><voice>2</voice><type>quarter</type></note>
      <barline location="right"><bar-style>light-heavy</bar-style></barline>
    </measure>
  </part>
  <part id="P2">
    <measure number="1">
      <attributes><divisions>12</divisions>
        <clef><sign>percussion</sign><line>2</line></clef></attributes>
      <direction><direction-type><words>pizz.</words></direction-type></direction>
      <direction><direction-type><words>with sticks</words></direction-type></direction>
      <note><unpitched><display-step>C</display-step><display-octave>5</display-octave></unpitched>
        <duration>24</duration><instrument id="P2-I39"/><voice>1</voice><type>half</type></note>
      <note><rest/><duration>24</duration><voice>1</voice><type>half</type></note>
    </measure>
    <measure number="2">
      <note><rest measure="yes"/><duration>48</duration><voice>1</voice></note>
    </measure>
  </part>
</score-partwise>`

describe('parseXml', () => {
  it('reads elements, attributes, text and entities, and can drop subtrees', () => {
    const root = parseXml(SCORE, new Set(['defaults']))
    const score = child(root, 'score-partwise')!
    expect(score.attrs.version).toBe('4.0')
    expect(child(child(score, 'work'), 'work-title')!.text).toBe('Import & Test')
    expect(child(score, 'defaults')).toBeUndefined()
    expect(childrenOf(score, 'part').map(part => part.attrs.id)).toEqual(['P1', 'P2'])
  })
})

describe('importMusicXml', () => {
  const { music, kitNotes, skipped } = importMusicXml(SCORE)
  const clarinet = music.tracks[0]!
  const voice = (measure: number, index = 0) =>
    clarinet.staves[0]!.measures[measure]!.voices.find(v => v.index === index)!
  const chordAt = (measure: number, event: number, index = 0) =>
    voice(measure, index).events[event] as Chord

  it('produces a valid score', () => {
    expect(validate(music)).toEqual([])
    expect(music).toMatchObject({ title: 'Import & Test', composer: 'Nobody', tpq: 960 })
    expect(music.measures).toHaveLength(2)
    expect(music.measures[1]!.barline).toBe('final')
  })

  it('stores sounding pitch, concert key and the transposition', () => {
    // Written D5 on a B♭ clarinet sounds C5; written D major is concert C major.
    expect(midiNumber((chordAt(0, 0).notes[0] as Note).pitch)).toBe(72)
    expect(music.measures[0]!.key!.accidentals).toBe(0)
    expect(clarinet.transposition).toEqual({ diatonic: -1, chromatic: -2, octave: 0 })
    expect(music.measures[0]!.tempos).toMatchObject([{ bpm: 120, tick: 0 }])
  })

  it('groups tuplets and joins chords', () => {
    const tuplet = voice(0).events[2] as Tuplet
    expect(tuplet).toMatchObject({ kind: 'tuplet', actual: 3, normal: 2 })
    expect(tuplet.unit).toEqual({ type: -1, dots: 0 })
    expect(tuplet.events).toHaveLength(3)
    expect(chordAt(1, 1).notes).toHaveLength(2)
  })

  it('links ties, slurs, grace notes and articulations', () => {
    const tied = chordAt(0, 3).notes[0] as Note
    expect(tied.tie).toEqual([{ to: chordAt(1, 0).notes[0]!.id }])
    expect(clarinet.trackSpanners.find(s => s.kind === 'slur')).toMatchObject({
      start: { id: chordAt(0, 0).id }, end: { id: chordAt(0, 1).id },
    })
    expect(chordAt(1, 1).graceNotes).toMatchObject({ slash: true, chords: [{ kind: 'chord' }] })
    expect(chordAt(0, 1).articulations.map(a => a.type)).toEqual(['staccato', 'accent'])
  })

  it('places dynamics, hairpins and one-note techniques', () => {
    const dynamics = clarinet.attachments.filter(a => a.kind === 'dynamic')
    expect(dynamics.map(d => [d.type, d.at])).toEqual([
      ['p', { type: 'tick', measureId: 'm1', tick: 0 }],
      ['f', { type: 'tick', measureId: 'm1', tick: 2880 }],
    ])
    expect(clarinet.trackSpanners.find(s => s.kind === 'hairpin')).toMatchObject({
      type: 'cresc', start: { tick: 0 }, end: { tick: 2880 },
    })
    expect(clarinet.attachments.find(a => a.kind === 'technique')).toMatchObject({
      techniqueId: 'core:tremolo', scope: 'this', at: { id: chordAt(1, 0).id },
    })
  })

  it('fills a second voice with spaces around what is written', () => {
    expect(voice(1, 1).events.map(event => event.kind)).toEqual(['space', 'chord', 'space'])
  })

  it('builds a kit for unpitched parts and keeps full-measure rests', () => {
    const snare = music.tracks[1]!
    expect(snare.kit).toMatchObject([{ name: 'Snare', sound: 'snare', displayPitch: 1 }])
    const firstChord = snare.staves[0]!.measures[0]!.voices[0]!.events[0] as Chord
    const hit = firstChord.notes[0] as UnpitchedNote
    expect(hit.kitEntryId).toBe(snare.kit![0]!.id)
    expect(kitNotes).toEqual({ P2: { snare: 38 } })
    const rest = snare.staves[0]!.measures[1]!.voices[0]!.events[0] as Rest
    expect(rest).toMatchObject({ kind: 'rest', fullMeasure: true, duration: { type: 2, dots: 0 } })
    // Words that are no technique are kept as text for the page.
    expect(snare.attachments).toMatchObject([
      { kind: 'technique', techniqueId: 'core:pizz' },
      { kind: 'text', text: 'with sticks' },
    ])
  })

  it('counts what it had to leave out', () => {
    expect(skipped).toEqual({
      'marcato (strong accent) stored as a plain accent': 1,
      '<fermata>': 1,
    })
  })

  it('plays through the Cinematic Studio setup', () => {
    const setup = cinematicSetup(music, kitNotes)
    expect(setup.trackProfiles.P1!.outputs[0]!.instrumentProfileId).toBe('csw')
    const { tracks, warnings } = play(music, setup)
    expect(warnings).toEqual([])

    const wind = tracks[0]!.messages
    // The profile's numbers are tuned by ear, so only the shape is checked here:
    // the accented staccato note makes the instrument switch articulation and back.
    const switches = wind.filter(m => m.source === 'technique-cc'
      || (m.source === 'keyswitch' && m.type === 'note-on'))
    expect(switches.length).toBeGreaterThanOrEqual(3)
    // The slurred second note is moved ahead of its beat for the legato transition.
    const second = wind.filter(m => m.source === 'note' && m.type === 'note-on')[1]!
    expect(second.timeMs).toBeLessThan(500)
    // Dynamics go to the profile's dynamics controller and rise through the crescendo.
    const profile = setup.instrumentProfiles.find(p => p.id === 'csw')!
    const controller = profile.curveMappings[0]!.cc![0]!.number
    const dynamics = wind.filter(m => m.type === 'cc' && m.data1 === controller)
      .map(m => m.data2)
    expect(dynamics[0]).toBeLessThan(dynamics.at(-1)!)

    // The snare is a Symphony Series patch of its own: a normal hit is key 48.
    const drum = tracks[1]!.messages.filter(m => m.type === 'note-on')
    expect(drum.map(m => [m.channel, m.data1])).toEqual([[1, 48]])
  })
})

/** A dynamic level (0–1) as MIDI, through a profile's output range and its curve. */
const mapped = (range: OutputRange, value: number) => Math.round(range.low
  + (range.high - range.low) * (range.curve
    ? shape(range.curve.curveType, range.curve.steepness, value)
    : value))

/** A one-part score in 4/4 at 120, 12 divisions to the quarter, with the given bars. */
const mini = (partName: string, ...bars: string[]) => `<score-partwise>
  <part-list><score-part id="P1"><part-name>${partName}</part-name></score-part></part-list>
  <part id="P1">${bars.map((bar, i) => `<measure number="${i + 1}">${i > 0 ? '' : `
    <attributes><divisions>12</divisions><time><beats>4</beats><beat-type>4</beat-type></time>
      <clef><sign>G</sign><line>2</line></clef></attributes>
    <direction><direction-type><metronome><beat-unit>quarter</beat-unit>
      <per-minute>120</per-minute></metronome></direction-type></direction>`}${bar}
  </measure>`).join('')}</part>
</score-partwise>`

const direction = (inner: string) =>
  `<direction><direction-type>${inner}</direction-type></direction>`
const notations = (inner: string) => `<notations>${inner}</notations>`

const playMini = (source: string) => {
  const { music, kitNotes } = importMusicXml(source)
  expect(validate(music)).toEqual([])
  const setup = cinematicSetup(music, kitNotes)
  const result = play(music, setup)
  const noteOns = result.tracks[0]!.messages
    .filter(m => m.source === 'note' && m.type === 'note-on')
  return { music, setup, ...result, noteOns, messages: result.tracks[0]!.messages }
}

describe('playing what the notes alone do not say', () => {
  it('reads rit. with its dashed line as a tempo change', () => {
    const { music, tempo } = playMini(mini(
      'Flute',
      note('C', 5, 48, 'whole'),
      `${direction('<words>rit.</words><dashes type="start" number="1"/>')}
       ${note('C', 5, 48, 'whole')}${direction('<dashes type="stop" number="1"/>')}`,
    ))
    expect(music.globalSpanners).toMatchObject([{
      kind: 'tempo-change',
      type: 'rit',
      start: { measureId: 'm2', tick: 0 },
      end: { measureId: 'm2', tick: 3840 },
    }])
    expect(tempo.tickToMs(3840)).toBeCloseTo(2000)
    expect(tempo.tickToMs(7680) - tempo.tickToMs(3840)).toBeGreaterThan(2200)
  })

  it('aims a rit. at the tempo mark it leads into', () => {
    const slower = direction(
      '<metronome><beat-unit>quarter</beat-unit><per-minute>60</per-minute></metronome>',
    )
    const { music, tempo } = playMini(mini(
      'Flute',
      `${direction('<words>rit.</words>')}${note('C', 5, 48, 'whole')}`,
      `${slower}${note('C', 5, 48, 'whole')}`,
    ))
    expect(music.globalSpanners).toMatchObject([{ type: 'rit', targetTempo: 60 }])
    expect(tempo.points.at(-1)).toEqual({ tick: 3840, qpm: 60 })
  })

  it('plays a measured tremolo as one note of its written length', () => {
    const stroke = notations('<ornaments><tremolo type="single">1</tremolo></ornaments>')
    const { noteOns, messages } = playMini(mini(
      'Flute', `${note('C', 5, 24, 'half', stroke)}${note('D', 5, 24, 'half')}`,
    ))
    // The library plays the repetitions. The plain half note after it is on time: a
    // tremolo is not connected to the note after it.
    expect(noteOns.map(m => [Math.round(m.timeMs), m.data1])).toEqual([[0, 72], [1000, 74]])
    const off = messages.find(m => m.type === 'note-off' && m.data1 === 72)!
    expect(off.timeMs).toBeGreaterThan(900)
    expect(messages.filter(m => m.source === 'keyswitch' && m.type === 'note-on')
      .map(m => m.data1)).toEqual([26, 24])
  })

  it('reads character words as connected or detached phrasing', () => {
    const words = (text: string) => direction(`<words>${text}</words>`)
    const quarters = ['C', 'D', 'E', 'F'].map(step => note(step, 5, 12, 'quarter')).join('')
    const { music, noteOns } = playMini(mini(
      'Trumpet',
      `${words('Maestoso')}${quarters}`,
      `${words('molto cantabile')}${quarters}`,
      `${words('non legato')}${quarters}`,
    ))
    expect(music.tracks[0]!.attachments.map(a => a.kind === 'technique' && a.techniqueId))
      .toEqual(['core:detached', 'core:connected', 'core:detached'])
    // Only the cantabile bar is slurred: its notes after the first come in early.
    const late = noteOns.map((m, i) => Math.round(m.timeMs) - i * 500)
    expect(late.map(ms => ms < 0)).toEqual([
      false, false, false, false,
      false, true, true, true,
      false, false, false, false,
    ])
  })

  it('rolls an arpeggiated chord upwards', () => {
    const rolled = notations('<arpeggiate/>')
    const chordNote = (step: string) => `<note><chord/><pitch><step>${step}</step>
      <octave>5</octave></pitch><duration>48</duration><voice>1</voice><type>whole</type></note>`
    const { noteOns } = playMini(mini(
      'Harp', `${note('G', 5, 48, 'whole', rolled)}${chordNote('C')}${chordNote('E')}`,
    ))
    expect(noteOns.map(m => m.data1)).toEqual([72, 76, 79])
    expect(noteOns[0]!.timeMs).toBeLessThan(noteOns[1]!.timeMs)
    expect(noteOns[1]!.timeMs).toBeLessThan(noteOns[2]!.timeMs)
  })

  it('fills a glissando with the notes in between', () => {
    const line = (type: string) => notations(`<glissando type="${type}" number="1"/>`)
    const { noteOns } = playMini(mini(
      'Harp', `${note('C', 5, 24, 'half', line('start'))}${note('G', 5, 24, 'half', line('stop'))}`,
    ))
    expect(noteOns.map(m => m.data1)).toEqual([72, 73, 74, 75, 76, 77, 78, 79])
    // The run takes the second half of the first note.
    expect(noteOns[1]!.timeMs).toBeCloseTo(500)
    expect(noteOns.at(-1)!.timeMs).toBeCloseTo(1000)
  })

  it('plays a fast slurred run with marcato, held into the next note, not moved ahead', () => {
    const slur = (type: string) => notations(`<slur type="${type}" number="1"/>`)
    const { messages, noteOns } = playMini(mini(
      'Flute',
      `${note('C', 5, 3, '16th', slur('start'))}${note('D', 5, 3, '16th')}
       ${note('E', 5, 3, '16th')}${note('F', 5, 3, '16th', slur('stop'))}
       ${note('G', 5, 36, 'half', '<dot/>')}`,
    ))
    expect(noteOns.slice(0, 4).map(m => Math.round(m.timeMs))).toEqual([0, 125, 250, 375])
    // One articulation for the run, another for the long note after it.
    const switches = messages.filter(m => m.source === 'technique-cc'
      || (m.source === 'keyswitch' && m.type === 'note-on'))
    expect(switches).toHaveLength(2)
    expect(switches[0]!.timeMs).toBeLessThanOrEqual(noteOns[0]!.timeMs)
    const firstOff = messages.find(m => m.type === 'note-off' && m.data1 === 72)!
    expect(firstOff.timeMs).toBeGreaterThan(125)
  })

  const presses = (messages: { source: string, type: string, data1: number }[]) => messages
    .filter(m => m.source === 'keyswitch' && m.type === 'note-on').map(m => m.data1)
  const slur = (type: string, more = '') => notations(`${more}<slur type="${type}" number="1"/>`)
  const accent = '<articulations><accent/></articulations>'

  it('plays a repeated pitch in a slur detached, on its beat', () => {
    const { noteOns, events } = playMini(mini('Flute',
      `${note('C', 5, 12, 'quarter', slur('start'))}${note('D', 5, 12, 'quarter')}
       ${note('D', 5, 12, 'quarter')}${note('E', 5, 12, 'quarter', slur('stop'))}`))
    // C–D and D–E are slurred; the second D starts the second phrase.
    expect(events.filter(e => e.kind === 'note-event').map(e => e.kind === 'note-event' && e.slur))
      .toEqual(['first', 'last', 'first', 'last'])
    const at = noteOns.map(m => Math.round(m.timeMs))
    expect(at[0]).toBe(0)
    expect(at[1]).toBeLessThan(500) // slurred into: early
    expect(at[2]).toBe(1000) // the repeat: on its beat
    expect(at[3]).toBeLessThan(1500)
    // Still one written slur: one swell over it, rising at its first note only.
    const rises = events.filter(e => e.kind === 'curve-event' && e.combine === 'scale'
      && e.segmentIndex === 0).map(e => e.kind === 'curve-event' && e.startTick)
    expect(rises).toEqual([0])
  })

  it('swells every slur that is not a fast run, even from a short first note', () => {
    // A triplet eighth at 120 is 167 ms: shorter than a note needs to swell on its own
    // (200 ms), but slower than a fast run (160 ms).
    const { events } = playMini(mini('Flute',
      `${note('C', 5, 4, 'eighth', `${triplet('start')}${slur('start')}`)}
       ${note('D', 5, 4, 'eighth', triplet(''))}${note('E', 5, 4, 'eighth', triplet('stop'))}
       ${note('F', 5, 36, 'half', `<dot/>${slur('stop')}`)}`))
    const rises = events.filter(e => e.kind === 'curve-event' && e.combine === 'scale'
      && e.segmentIndex === 0).map(e => e.kind === 'curve-event' && e.startTick)
    expect(rises).toEqual([0])
  })

  it('rises out of a slow first note and falls into a slow last one, not over a run', () => {
    const swellsOf = (bar: string) => playMini(mini('Flute', bar)).events
      .filter(e => e.kind === 'curve-event' && e.combine === 'scale' && e.durationTicks > 0)
      .map(e => e.kind === 'curve-event' && (e.segmentIndex === 0 ? 'rise' : 'fall'))
    const sixteenths = (from: number, to: number, last = '') => ['C', 'D', 'E', 'F']
      .slice(from, to).map((step, i, all) =>
        note(step, 5, 3, '16th', i === all.length - 1 ? last : '')).join('')
    // A fast run into a long note: no rise, a fall.
    expect(swellsOf(`${note('C', 5, 3, '16th', slur('start'))}${sixteenths(1, 4)}
      ${note('G', 5, 36, 'half', `<dot/>${slur('stop')}`)}`)).toEqual(['fall'])
    // A long note into a fast run: a rise, no fall.
    expect(swellsOf(`${note('G', 5, 24, 'half', slur('start'))}${sixteenths(0, 4)}
      ${sixteenths(0, 4, slur('stop'))}`)).toEqual(['rise'])
  })

  it('ends a slur on a tied note where its tied-over part ends', () => {
    const tie = (type: string) => `<tie type="${type}"/>`
    const tied = (type: string, more = '') => notations(`<tied type="${type}"/>${more}`)
    const { messages, events } = playMini(mini('Flute',
      `${note('C', 5, 12, 'quarter', slur('start'))}
       ${note('D', 5, 36, 'half', `<dot/>${tie('start')}${tied('start')}`)}`,
      `${note('D', 5, 12, 'quarter',
        `${tie('stop')}${tied('stop', '<slur type="stop" number="1"/>')}`)}
       ${note('E', 5, 12, 'quarter')}${note('F', 5, 24, 'half')}`))
    // The tied D is the slur's last note; E–F after it is a phrase of its own.
    const notes = events.filter(e => e.kind === 'note-event')
    expect(notes.map(e => e.kind === 'note-event' && e.slur))
      .toEqual(['first', 'last', 'first', 'last'])
    // D is let go before E comes, so E starts fresh instead of being slurred into.
    const offD = messages.find(m => m.source === 'note' && m.type === 'note-off' && m.data1 === 74)!
    const onE = messages.find(m => m.source === 'note' && m.type === 'note-on' && m.data1 === 76)!
    expect(onE.timeMs).toBe(2500)
    expect(offD.timeMs).toBeLessThan(onE.timeMs)
    // A swell over each phrase.
    const rises = events.filter(e => e.kind === 'curve-event' && e.combine === 'scale'
      && e.segmentIndex === 0).map(e => e.kind === 'curve-event' && e.startTick)
    expect(rises).toEqual([0, notes[2]!.startTick])
  })

  it('starts a slow slur with sustain, whatever ran before it', () => {
    // A fast run, then a slow slur right after it: only the run is marcato.
    const { messages } = playMini(mini(
      'Flute',
      `${note('C', 5, 3, '16th', slur('start'))}${note('D', 5, 3, '16th')}
       ${note('E', 5, 3, '16th')}${note('F', 5, 3, '16th', slur('stop'))}
       ${note('G', 5, 24, 'half', slur('start'))}${note('A', 5, 12, 'quarter', slur('stop'))}`,
    ))
    expect(presses(messages)).toEqual([30, 24])
  })

  it('opens a slur with marcato on an accent, at the velocity the note has', () => {
    // Slow: marcato for the accent, back to sustain for the rest of the slur.
    const slow = playMini(mini('Flute',
      `${note('C', 5, 24, 'half', slur('start', accent))}
       ${note('D', 5, 24, 'half', slur('stop'))}`))
    expect(presses(slow.messages)).toEqual([30, 24])
    // Fast: the accent is not the soft start a run otherwise gets.
    const fast = playMini(mini('Flute',
      `${note('C', 5, 3, '16th', slur('start', accent))}${note('D', 5, 3, '16th')}
       ${note('E', 5, 3, '16th')}${note('F', 5, 3, '16th', slur('stop'))}
       ${note('G', 5, 36, 'half', '<dot/>')}`))
    expect(presses(fast.messages)).toEqual([30, 24])
    expect(fast.noteOns[0]!.data2).toBe(slow.noteOns[0]!.data2)
    expect(fast.noteOns[0]!.data2).toBeGreaterThan(80) // not the run's soft 0.1
  })

  it('plays a tremolo between two notes as a trill: both keys together', () => {
    const tremolo = (type: string) =>
      notations(`<ornaments><tremolo type="${type}">3</tremolo></ornaments>`)
    const { messages, noteOns, music } = playMini(mini('Violin',
      `${note('C', 5, 24, 'half', tremolo('start'))}${note('D', 5, 24, 'half', tremolo('stop'))}`))
    expect(music.tracks[0]!.trackSpanners).toMatchObject([
      { kind: 'custom-spanner', techniqueId: 'core:tremolo-pair' },
    ])
    expect(presses(messages)).toEqual([27])
    expect(noteOns.map(m => [m.data1, m.timeMs])).toEqual([[72, 0], [74, 0]])
    const offs = messages.filter(m => m.source === 'note' && m.type === 'note-off')
    expect(offs[0]!.timeMs).toBeCloseTo(offs[1]!.timeMs)
    expect(offs[0]!.timeMs).toBeGreaterThan(1500)
  })

  it('plays a trill with its upper note, read from the key, the bar and the mark', () => {
    const trill = (accidental = '') => notations(
      `<ornaments><trill-mark/>${accidental && `<accidental-mark>${accidental}</accidental-mark>`}
       </ornaments>`)
    const key = (fifths: number) => `<attributes><key><fifths>${fifths}</fifths></key></attributes>`
    const pitches = (bar: string) => {
      const { noteOns, messages } = playMini(mini('Flute', bar))
      const last = noteOns.filter(m => m.timeMs === noteOns.at(-1)!.timeMs)
      return { keys: presses(messages), trill: last.map(m => m.data1).sort((a, b) => a - b) }
    }
    // C major: E trills with F, a half step.
    expect(pitches(`${note('E', 5, 48, 'whole', trill())}`))
      .toEqual({ keys: [27], trill: [76, 77] })
    // D major: with F sharp, a whole step.
    expect(pitches(`${key(2)}${note('E', 5, 48, 'whole', trill())}`).trill).toEqual([76, 78])
    // An F sharp earlier in the bar counts too.
    const sharpF = `<note><pitch><step>F</step><alter>1</alter><octave>5</octave></pitch>
      <duration>24</duration><voice>1</voice><type>half</type></note>`
    expect(pitches(`${sharpF}${note('E', 5, 24, 'half', trill())}`).trill).toEqual([76, 78])
    // The accidental on the trill wins over the key: D with E flat.
    expect(pitches(`${key(2)}${note('D', 5, 48, 'whole', trill('flat'))}`).trill)
      .toEqual([74, 75])
  })

  it('holds double tongue at its "Hold" setting, which CC1 sets in the brass', () => {
    const measured = notations('<ornaments><tremolo type="single">2</tremolo></ornaments>')
    const held = (part: string) => playMini(mini(part,
      `${note('C', 5, 24, 'half', measured)}${note('E', 5, 24, 'half')}`)).messages
      .filter(m => m.source === 'technique-cc' && m.data1 === 1).map(m => m.data2)
    expect(held('Trumpet')).toEqual([127])
    // And let go early: 0.4 of the half note it plays (0.95 of 1000 ms, detached).
    const trumpet = playMini(mini('Trumpet',
      `${note('C', 5, 24, 'half', measured)}${note('E', 5, 24, 'half')}`)).messages
    const off = trumpet.find(m => m.source === 'note' && m.type === 'note-off')!
    expect(off.timeMs).toBeCloseTo(1000 * 0.95 * 0.4)
    expect(held('Flute')).toEqual([])
    expect(held('Violin')).toEqual([])
  })

  it('picks the tremolo patch of each family', () => {
    const unmeasured = notations('<ornaments><tremolo type="single">3</tremolo></ornaments>')
    const measured = notations('<ornaments><tremolo type="single">1</tremolo></ornaments>')
    const keys = (part: string, mark: string) => presses(playMini(mini(part,
      `${note('C', 5, 24, 'half', mark)}${note('E', 5, 24, 'half')}`)).messages)
    // Flutter tongue for winds, tremolo for strings; then sustain for the plain note.
    expect(keys('Flute', unmeasured)).toEqual([28, 24])
    expect(keys('Trumpet', unmeasured)).toEqual([28, 24])
    expect(keys('Violin', unmeasured)).toEqual([25, 24])
    // Measured: repetitions, double tongue, measured tremolo, all on the same key.
    for (const part of ['Flute', 'Trumpet', 'Violin'])
      expect(keys(part, measured)).toEqual([26, 24])
  })

  it('plays sfp as a loud marcato attack over a soft CC1', () => {
    const sfp = direction('<dynamics><sfp/></dynamics>')
    const { messages, noteOns, setup } = playMini(mini('Horn',
      `${sfp}${note('C', 4, 48, 'whole')}`))
    expect(presses(messages)).toEqual([30])
    // Velocity 0.9 and CC1 at piano (0.2, less under the swell), on the profile's ranges.
    const profile = setup.instrumentProfiles
      .find(p => p.id === setup.trackProfiles.P1!.outputs[0]!.instrumentProfileId)!
    if (!('mapping' in profile.velocity)) throw new Error('no velocity range')
    expect(noteOns[0]!.data2).toBe(mapped(profile.velocity.mapping, 0.9))
    const cc1 = messages.filter(m => m.type === 'cc' && m.data1 === 1)
    expect(cc1[0]!.timeMs).toBe(0)
    const ccRange = profile.curveMappings[0]!.cc![0]!.mapping
    expect(cc1[0]!.data2).toBeLessThanOrEqual(mapped(ccRange, 0.2))
  })

  it('shifts patches that are not mapped at sounding pitch', () => {
    const piccolo = playMini(mini('Piccolo', note('C', 6, 48, 'whole')))
    expect(piccolo.setup.trackProfiles.P1!.outputs).toEqual([
      { id: 'main', instrumentProfileId: 'csw-piccolo' },
    ])
    expect(piccolo.noteOns[0]!.data1).toBe(84 - 12)
    const tuba = playMini(mini('Tuba', note('C', 2, 48, 'whole')))
    expect(tuba.noteOns[0]!.data1).toBe(36 + 12)
    // The contrabassoon is a woodwind at its own pitch, not a contrabass.
    const contrabassoon = playMini(mini('Contrabassoon', note('C', 2, 48, 'whole')))
    expect(contrabassoon.setup.trackProfiles.P1!.outputs[0]!.instrumentProfileId).toBe('csw')
    expect(contrabassoon.noteOns[0]!.data1).toBe(36)
  })
})

describe('Symphony Series percussion', () => {
  const hit = (type: string, duration: number, extra = '') => `<note><unpitched>
    <display-step>C</display-step><display-octave>5</display-octave></unpitched>
    <duration>${duration}</duration><voice>1</voice><type>${type}</type>${extra}</note>`
  const rest = (type: string, duration: number) =>
    `<note><rest/><duration>${duration}</duration><voice>1</voice><type>${type}</type></note>`
  const roll = notations('<ornaments><tremolo type="single">3</tremolo></ornaments>')
  const accent = notations('<articulations><accent/></articulations>')
  const staccato = notations('<articulations><staccato/></articulations>')
  const wedge = (type: string) => direction(`<wedge type="${type}"/>`)
  const keys = (messages: { source: string, type: string, data1: number }[], source: string) =>
    messages.filter(m => m.source === source && m.type === 'note-on').map(m => m.data1)

  it('alternates two keys for hits, edge hits for accents on the snare', () => {
    const { messages } = playMini(mini('Snare Drum',
      `${hit('quarter', 12)}${hit('quarter', 12)}
       ${hit('quarter', 12, accent)}${hit('quarter', 12)}`))
    expect(keys(messages, 'note')).toEqual([48, 50, 52, 48])
  })

  it('plays a roll as one note on the roll key, with CC1 for its dynamics', () => {
    const { messages } = playMini(mini('Bass Drum',
      `${wedge('crescendo')}${hit('whole', 48, roll)}${wedge('stop')}`, hit('quarter', 12)))
    expect(keys(messages, 'note')).toEqual([55, 48])
    const cc1 = messages.filter(m => m.type === 'cc' && m.data1 === 1).map(m => m.data2)
    expect(cc1.at(-1)).toBeGreaterThan(cc1[0]!)
  })

  it('swells a suspended cymbal into its peak: crescendo sample when short, roll when long', () => {
    // A long hit; a one-second roll under a crescendo: the 1.25 s sample, ending on the
    // accented hit it leads to. Soft mallets, hard ones for the accent.
    const short = playMini(mini('Suspended Cymbal',
      `${hit('half', 24)}${wedge('crescendo')}${hit('half', 24, roll)}${wedge('stop')}`,
      hit('quarter', 12, accent)))
    expect(keys(short.messages, 'note')).toEqual([48, 57, 50])
    expect(keys(short.messages, 'keyswitch')).toEqual([38, 36, 45]) // damped at the end
    const sample = short.messages.filter(m => m.source === 'note' && m.data1 === 57)
    expect(sample.map(m => Math.round(m.timeMs))).toEqual([2000 - 1250, 2000])
    // A roll over two bars (four seconds) rolls, with CC1 on the ease curve.
    const tie = (type: string) => `<tie type="${type}"/>`
    const long = playMini(mini('Suspended Cymbal',
      `${wedge('crescendo')}${hit('whole', 48, `${tie('start')}${roll}`)}`,
      `${hit('whole', 48, `${tie('stop')}${roll}`)}${wedge('stop')}`))
    expect(keys(long.messages, 'note')).toEqual([55])
  })

  it('switches the timpani to rolls, damps it at rests and plays it an octave up', () => {
    const { messages } = playMini(mini('Timpani',
      `${note('C', 3, 12, 'quarter')}${rest('quarter', 12)}${note('G', 2, 24, 'half', roll)}`))
    expect(keys(messages, 'note')).toEqual([48 + 12, 43 + 12])
    // Hits, damp after the first note, rolls, damp at the end.
    expect(keys(messages, 'keyswitch')).toEqual([36, 45, 38, 45])
    // Held over the rest: until just before the next note (its keyswitch lead), or for a
    // beat when nothing follows; letting go switches back to hits.
    const lead = timpaniProfile.keyswitchLeadMs ?? 0
    const damp = messages.filter(m => m.source === 'keyswitch' && m.data1 === 45)
      .map(m => [m.type, Math.round(m.timeMs)])
    expect(damp).toEqual([
      ['note-on', 500], ['note-off', 1000 - lead], ['note-on', 2000], ['note-off', 2500],
    ])
  })

  it('damps the timpani right after a staccato note too', () => {
    const lead = timpaniProfile.keyswitchLeadMs ?? 0
    const { messages } = playMini(mini('Timpani',
      `${note('C', 3, 12, 'quarter', staccato)}${note('C', 3, 12, 'quarter')}
       ${rest('half', 24)}`))
    const damp = messages.filter(m => m.source === 'keyswitch' && m.data1 === 45)
      .map(m => [m.type, Math.round(m.timeMs)])
    // Half the staccato quarter, until the next note; then at the rest.
    expect(damp).toEqual([
      ['note-on', 250], ['note-off', 500 - lead], ['note-on', 1000], ['note-off', 1500],
    ])
  })

  it('ends a roll where its tremolo ends, and damps only at the rest after the tie', () => {
    const tie = (type: string) => `<tie type="${type}"/>`
    // A timpani roll for a half note, tied into a half note without one, then a rest.
    const { messages } = playMini(mini('Timpani',
      `${note('G', 2, 24, 'half', `${tie('start')}${roll}`)}
       ${note('G', 2, 24, 'half', tie('stop'))}`,
      rest('whole', 48)))
    const notes = messages.filter(m => m.source === 'note').map(m => [m.type, Math.round(m.timeMs)])
    expect(notes).toEqual([['note-on', 0], ['note-off', 1000]])
    const damp = messages.find(m => m.source === 'keyswitch' && m.data1 === 45)!
    expect(damp.timeMs).toBeCloseTo(2000)
  })

  it('plays a measured tremolo as written-out hits, an unmeasured one as a roll', () => {
    const measured = notations('<ornaments><tremolo type="single">1</tremolo></ornaments>')
    const { messages } = playMini(mini('Snare Drum',
      `${hit('half', 24, measured)}${hit('half', 24, roll)}`))
    // Eighths on the half note, two keys in turn; then one roll note.
    expect(keys(messages, 'note')).toEqual([48, 50, 48, 50, 55])
  })

  it('starts an sfp roll with a stroke: with it on drums, before it on timpani', () => {
    const sfp = direction('<dynamics><sfp/></dynamics>')
    const at = (messages: { source: string, type: string, data1: number, timeMs: number }[],
      source: string) => messages.filter(m => m.source === source && m.type === 'note-on')
      .map(m => [Math.round(m.timeMs), m.data1])
    const drum = playMini(mini('Snare Drum', `${sfp}${hit('whole', 48, roll)}`))
    expect(at(drum.messages, 'note')).toEqual([[0, 48], [0, 55]])
    // Timpani: a 32nd stroke on the hits keyswitch, then the roll keyswitch and the roll.
    const timpani = playMini(mini('Timpani', `${sfp}${note('G', 2, 48, 'whole', roll)}`))
    expect(at(timpani.messages, 'note')).toEqual([[0, 55], [63, 55]])
    const lead = timpaniProfile.keyswitchLeadMs ?? 0
    expect(at(timpani.messages, 'keyswitch')).toEqual([[0, 36], [63 - lead, 38], [2000, 45]])
  })

  it('gives each written-out tremolo note the dynamics where it starts', () => {
    // A whole-note roll on the xylophone, p to f: the strokes get louder all the way.
    const { noteOns } = playMini(mini('Xylophone',
      `${direction('<dynamics><p/></dynamics>')}${wedge('crescendo')}
       ${note('C', 5, 48, 'whole', roll)}${wedge('stop')}`,
      `${direction('<dynamics><f/></dynamics>')}${note('C', 5, 12, 'quarter')}`))
    const strokes = noteOns.slice(0, -1).map(m => m.data2)
    expect(strokes.length).toBeGreaterThan(10)
    strokes.slice(1).forEach((velocity, i) => expect(velocity).toBeGreaterThanOrEqual(strokes[i]!))
    expect(strokes.at(-1)!).toBeGreaterThan(strokes[0]!)
  })

  it('shapes percussion hairpins exponentially, the suspended cymbal\'s on an ease', () => {
    const shapes = (part: string) => playMini(mini(part,
      `${wedge('crescendo')}${hit('whole', 48, roll)}${wedge('stop')}`, hit('quarter', 12)))
      .events.filter(e => e.kind === 'curve-event' && e.durationTicks > 0)
      .map(e => e.kind === 'curve-event' && [e.curveType, e.steepness])
    expect(shapes('Bass Drum')).toEqual([['exp', 4]])
    expect(shapes('Suspended Cymbal')).toEqual([['ease', undefined]])
  })

  it('plays sfp as a loud hit over a soft roll', () => {
    const sfp = direction('<dynamics><sfp/></dynamics>')
    const { messages, noteOns } = playMini(mini('Bass Drum', `${sfp}${hit('whole', 48, roll)}`))
    // A loud hit (0.9), and piano (0.2) from the start, on the bass drum's own range.
    if (!('mapping' in bassDrum.velocity)) throw new Error('no velocity range')
    expect(noteOns[0]!.data2).toBe(mapped(bassDrum.velocity.mapping, 0.9))
    const cc1 = messages.filter(m => m.type === 'cc' && m.data1 === 1)
    expect(cc1[0]!.data2).toBe(mapped(bassDrum.curveMappings[0]!.cc![0]!.mapping, 0.2))
  })

  it('damps a suspended cymbal after a staccato note and at rests, unless let ring', () => {
    const damps = (bar: string) => playMini(mini('Suspended Cymbal', bar)).messages
      .filter(m => m.source === 'keyswitch' && m.data1 === 45)
      .map(m => [m.type, Math.round(m.timeMs)])
    // The staccato note sounds for half its length: damped from there until the next
    // note; then at the rest, for a beat.
    expect(damps(`${hit('quarter', 12, staccato)}${hit('quarter', 12)}${rest('half', 24)}`))
      .toEqual([['note-on', 250], ['note-off', 500], ['note-on', 1000], ['note-off', 1500]])
    // A note left to ring is not damped at the rest after it.
    const letRing = notations('<tied type="let-ring"/>')
    expect(damps(`${hit('half', 24, letRing)}${rest('half', 24)}`)).toEqual([])
  })

  it('gives an sfp cymbal roll a long hit, ringing as long as the roll', () => {
    const sfp = direction('<dynamics><sfp/></dynamics>')
    const { messages } = playMini(mini('Suspended Cymbal', `${sfp}${hit('whole', 48, roll)}`))
    const offs = messages.filter(m => m.source === 'note' && m.type === 'note-off')
      .map(m => [m.data1, Math.round(m.timeMs)])
    expect(offs).toEqual([[48, 2000], [55, 2000]])
    expect(keys(messages, 'keyswitch')).toEqual([36, 45]) // hard mallets for sfp; damp at the end
  })

  it('gives platti long hits, short ones for staccato, and damps them at rests', () => {
    const { messages } = playMini(mini('Cymbals',
      `${hit('quarter', 12)}${hit('quarter', 12, staccato)}
       ${hit('quarter', 12)}${rest('quarter', 12)}`))
    expect(keys(messages, 'note')).toEqual([48, 52, 50])
    // Hits; damp right after the staccato note, and at the rest.
    expect(keys(messages, 'keyswitch')).toEqual([36, 45, 45])
    const damp = messages.find(m => m.source === 'keyswitch' && m.data1 === 45)!
    expect(damp.timeMs).toBeCloseTo(750)
  })

  it('writes mallet tremolos out, and plays the glockenspiel an octave down', () => {
    const measured = notations('<ornaments><tremolo type="single">1</tremolo></ornaments>')
    const glock = playMini(mini('Glockenspiel', note('C', 6, 48, 'whole', measured)))
    // One stroke on a whole note: eighths, at written pitch an octave down.
    expect(glock.noteOns.map(m => m.data1)).toEqual(Array(8).fill(84 - 12))
    // Unmeasured: about 70 ms a note, so a half second is seven notes.
    const xylo = playMini(mini('Xylophone',
      `${note('C', 5, 12, 'quarter', roll)}${rest('quarter', 12)}${rest('half', 24)}`))
    expect(xylo.noteOns).toHaveLength(7)
  })

  /** A one-part kit score: named instruments with General MIDI notes, and their hits. */
  const kitMini = (partName: string, instruments: [string, string, number][], bar: string) =>
    mini(partName, bar).replace('</part-name>', `</part-name>
      ${instruments.map(([id, name]) => `<score-instrument id="${id}">
        <instrument-name>${name}</instrument-name></score-instrument>`).join('')}
      ${instruments.map(([id, , gm]) => `<midi-instrument id="${id}">
        <midi-unpitched>${gm + 1}</midi-unpitched></midi-instrument>`).join('')}`)
  /** A quarter-note hit on one kit instrument, at a staff position. */
  const drum = (id: string, step: string, octave: number) => `<note><unpitched>
    <display-step>${step}</display-step><display-octave>${octave}</display-octave></unpitched>
    <duration>12</duration><instrument id="${id}"/><voice>1</voice><type>quarter</type></note>`

  it('gives each tom its own pair of keys, lowest tom lowest', () => {
    const { messages } = playMini(kitMini('Toms',
      [['t1', 'High Tom', 48], ['t2', 'Mid Tom', 45],
        ['t3', 'Low Tom', 43], ['t4', 'Floor Tom', 41]],
      `${drum('t4', 'G', 4)}${drum('t4', 'G', 4)}${drum('t3', 'A', 4)}${drum('t2', 'C', 5)}`,
    ).replace('</measure>', `</measure><measure number="2">${drum('t1', 'E', 5)}
      <note><rest/><duration>36</duration><voice>1</voice><type>half</type><dot/></note>
      </measure>`))
    // Tom 4 (G2/A2, in turn) up to Tom 1 (G3/A3), by staff position.
    expect(keys(messages, 'note')).toEqual([43, 45, 48, 52, 55])
  })

  it('gives bongos white keys from C2, and wood blocks temple blocks', () => {
    const bongos = playMini(kitMini('Bongos',
      [['b1', 'High Bongo', 60], ['b2', 'Low Bongo', 61]],
      `${drum('b1', 'C', 5)}${drum('b2', 'A', 4)}${drum('b1', 'C', 5)}${drum('b2', 'A', 4)}`))
    expect(keys(bongos.messages, 'note')).toEqual([50, 48, 50, 48])
    const blocks = playMini(kitMini('Wood Blocks',
      [['w1', 'High Wood Block', 76], ['w2', 'Low Wood Block', 77]],
      `${drum('w1', 'C', 5)}${drum('w2', 'A', 4)}${drum('w1', 'C', 5)}${drum('w2', 'A', 4)}`))
    // Two blocks: the lowest and the highest of the five temple blocks.
    expect(keys(blocks.messages, 'note')).toEqual([55, 48, 55, 48])
  })

  it('moves a snare part\'s rim clicks to the sticks patch, on a track of its own', () => {
    const { tracks } = playMini(kitMini('Snare Drum',
      [['s1', 'Snare', 38], ['s2', 'Side Stick', 37]],
      `${drum('s1', 'C', 5)}${drum('s2', 'C', 5)}${drum('s1', 'C', 5)}${drum('s2', 'C', 5)}`))
    expect(tracks.map(t => t.name)).toEqual(['Snare Drum', 'Snare Drum – Sticks'])
    expect(tracks.map(t => keys(t.messages, 'note'))).toEqual([[48, 50], [48, 50]])
  })

  it('plays what the setup says once it is edited: keys and outputs', () => {
    const source = kitMini('Toms', [['t1', 'High Tom', 48], ['t2', 'Low Tom', 43]],
      `${drum('t2', 'A', 4)}${drum('t1', 'E', 5)}${drum('t2', 'A', 4)}${drum('t1', 'E', 5)}`)
    const { music, kitNotes } = importMusicXml(source)
    const setup = cinematicSetup(music, kitNotes)
    const toms = setup.trackProfiles.P1!
    // The import's guess: the lower tom is Tom 4, the higher one Tom 3.
    expect(toms.outputs).toEqual([{
      id: 'main', instrumentProfileId: 'ssp-toms',
      sounds: { 'low-tom': 'tom-4', 'high-tom': 'tom-3' },
    }])
    // Corrected in the editor: the low tom is Tom 1, the high tom gets a track of its own.
    toms.outputs[0]!.sounds = { 'low-tom': 'tom-1' }
    toms.outputs.push({
      id: 'high', name: 'High Tom', instrumentProfileId: 'ssp-other', sounds: { 'high-tom': 'hit' },
    })
    const { tracks, warnings } = play(music, setup)
    expect(tracks.map(t => [t.outputId, t.name, keys(t.messages, 'note')])).toEqual([
      ['main', 'Toms', [55, 57]],
      ['high', 'High Tom', [48, 50]],
    ])
    expect(warnings).toEqual([])
    // A sound left out of every map is reported, not played.
    toms.outputs.pop()
    expect(play(music, setup).warnings).toEqual(['track P1: no output plays the sound "high-tom"'])
  })

  it('plays a drum kit at the notes the file gives its sounds, for a drum instrument', () => {
    const { tracks } = playMini(kitMini('Drum Set',
      [['k', 'Acoustic Bass Drum', 35], ['s', 'Acoustic Snare', 38],
        ['h', 'High Tom', 48], ['l', 'Low Floor Tom', 41]],
      `${drum('k', 'F', 4)}${drum('s', 'C', 5)}${drum('h', 'E', 5)}${drum('l', 'A', 4)}`))
    expect(tracks.map(t => [t.name, t.messages.filter(m => m.source === 'note'
      && m.type === 'note-on').map(m => [m.channel, m.data1])])).toEqual([
      ['Drum Set', [[10, 35], [10, 38], [10, 48], [10, 41]]],
    ])
  })

  it('gives unpitched parts it does not know the hits layout', () => {
    const { messages, setup } = playMini(mini('Metal Wind Chimes', hit('whole', 48)))
    expect(setup.trackProfiles.P1!.outputs[0]!.instrumentProfileId).toBe('ssp-other')
    expect(keys(messages, 'note')).toEqual([48])
  })
})
