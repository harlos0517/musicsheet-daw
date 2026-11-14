import type MusicData from '../types/MusicData'
import type { KitEntry, Measure, StaffMeasure } from '../types/MusicData'
import { BarlineStyle } from '../types/MusicData'
import { ScoreBuilder } from './build'

/**
 * B♭ clarinet and drum kit, concert C major, 4/4.
 * Covers: a transposing instrument, a percussion staff with a kit, repeats with
 * first and second endings, segno / to-coda / coda with D.S. al Coda, a rit.,
 * and a technique marking.
 */
export const band = (): MusicData => {
  const b = new ScoreBuilder('band')

  const kit: KitEntry[] = [
    {
      id: 'kick', kind: 'kit-entry', name: 'Bass Drum', sound: 'kick',
      displayPitch: -3, notehead: 'normal',
    },
    {
      id: 'snare', kind: 'kit-entry', name: 'Snare', sound: 'snare',
      displayPitch: 1, notehead: 'normal',
    },
    {
      id: 'hihat', kind: 'kit-entry', name: 'Closed Hi-hat', sound: 'hihat-closed',
      displayPitch: 5, notehead: 'x',
    },
  ]

  const measures: Measure[] = [
    b.measure('band-m1', {
      timeSig: b.timeSig(4, 4),
      key: b.key(0),
      tempos: [b.tempo('4', 120)],
      repeatStart: true,
    }),
    b.measure('band-m2', { repeatEnd: { times: 2 } }),
    b.measure('band-m3', { barline: BarlineStyle.DOUBLE }),
    b.measure('band-m4', { markers: [{ id: 'band-segno', kind: 'marker', type: 'segno' }] }),
    b.measure('band-m5', { markers: [{ id: 'band-to-coda', kind: 'marker', type: 'to-coda' }] }),
    b.measure('band-m6', {
      barline: BarlineStyle.DOUBLE,
      jumps: [{
        id: 'band-ds',
        kind: 'jump',
        to: { type: 'marker', id: 'band-segno' },
        until: 'band-to-coda',
        then: 'band-coda',
        playRepeats: false,
        displayText: 'D.S. al Coda',
      }],
    }),
    b.measure('band-m7', {
      markers: [{ id: 'band-coda', kind: 'marker', type: 'coda' }],
      barline: BarlineStyle.FINAL,
    }),
  ]

  // Clarinet pitches are sounding (concert) pitch; the renderer shows them a tone higher.
  const clarinetMusic = [
    '4:C5 4:D5 4:E5 4:C5',
    '2:G4 2:r',
    '2:G4 4:E4 4:r',
    '8:A4 8:B4 8:C5 8:D5 4:E5 4:C5',
    '4:F5 4:E5 4:D5 4:C5',
    '1:G4',
    '2.:C5 4:r',
  ]
  const groove = (): StaffMeasure['voices'] => [
    b.voice(b.seq(
      '8:@hihat,@kick 8:@hihat 8:@hihat,@snare 8:@hihat '
      + '8:@hihat,@kick 8:@hihat 8:@hihat,@snare 8:@hihat',
    )),
  ]

  const clarinetStaff = {
    id: 'band-cl-staff',
    kind: 'staff' as const,
    staffSpanners: [],
    measures: measures.map((m, i) => b.staffMeasure(
      m.id,
      [b.voice(b.seq(clarinetMusic[i]!))],
      i === 0 ? [{ tick: 0, clef: b.treble() }] : [],
    )),
  }
  const drumStaff = {
    id: 'band-dr-staff',
    kind: 'staff' as const,
    staffSpanners: [],
    measures: measures.map((m, i) => i === 6
      ? b.staffMeasure(m.id, [b.voice(b.chord('2.', ['@kick', '@snare']), b.rest('4'))])
      : b.staffMeasure(m.id, groove(), i === 0 ? [{ tick: 0, clef: b.percussion() }] : [])),
  }

  return {
    title: 'Band Chart',
    composer: 'Example',
    tpq: 960,
    measures,
    globalSpanners: [
      {
        id: 'band-volta1', kind: 'volta',
        startMeasureId: 'band-m2', endMeasureId: 'band-m2', passes: [1],
      },
      {
        id: 'band-volta2', kind: 'volta',
        startMeasureId: 'band-m3', endMeasureId: 'band-m3', passes: [2], openEnd: true,
      },
      {
        id: 'band-rit',
        kind: 'tempo-change',
        type: 'rit',
        start: { type: 'tick', measureId: 'band-m7', tick: 0 },
        end: { type: 'tick', measureId: 'band-m7', tick: 3840 },
        targetTempo: 90,
      },
    ],
    tracks: [
      {
        id: 'band-cl',
        kind: 'track',
        instrumentName: 'Clarinet in B♭',
        instrumentAbbr: 'Cl.',
        transposition: { diatonic: -1, chromatic: -2, octave: 0 },
        staves: [clarinetStaff],
        trackSpanners: [],
        attachments: [{
          id: 'band-legato',
          kind: 'technique',
          techniqueId: 'core:legato',
          text: 'legato',
          scope: 'until-next',
          at: { type: 'tick', measureId: 'band-m4', tick: 0 },
        }],
      },
      {
        id: 'band-dr',
        kind: 'track',
        instrumentName: 'Drum Kit',
        instrumentAbbr: 'Dr.',
        kit,
        staves: [drumStaff],
        trackSpanners: [],
        attachments: [],
      },
    ],
  }
}
