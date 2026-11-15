import type MusicData from '@/core/types/MusicData'
import { ArticulationType, BarlineStyle, DynamicType } from '@/core/types/MusicData'
import { ScoreBuilder } from './build'

/**
 * A single-line melody in G major with a pickup bar.
 * Covers: pickup and short final bar, dotted notes, a triplet, an accidental,
 * a tie across the barline, articulations, a slur, dynamics and a hairpin.
 */
export const melody = (): MusicData => {
  const b = new ScoreBuilder('mel')
  const tpq = 960

  const measures = [
    b.measure('mel-m0', {
      timeSig: b.timeSig(4, 4),
      key: b.key(1),
      tempos: [b.tempo('4', 96, { text: 'Moderato' })],
      actualTicks: tpq, // pickup: two eighths
    }),
    b.measure('mel-m1'),
    b.measure('mel-m2'),
    b.measure('mel-m3'),
    // 3 beats: completes the pickup
    b.measure('mel-m4', { actualTicks: 3 * tpq, barline: BarlineStyle.FINAL }),
  ]

  const m1 = b.seq('4.:B4 8:A4 4:G4 4:B4')
  const m2 = [
    b.chord('8', ['C5'], { articulations: [ArticulationType.STACCATO] }),
    b.chord('8', ['B4'], { articulations: [ArticulationType.STACCATO] }),
    b.chord('8', ['A4'], { articulations: [ArticulationType.STACCATO] }),
    b.chord('8', ['G4'], { articulations: [ArticulationType.STACCATO] }),
    ...b.seq('4:F#4 4:A4'),
  ]
  const m3 = [
    b.tuplet(3, 2, '8', b.seq('8:B4 8:C#5 8:D5')),
    b.chord('4', ['E5'], { articulations: [ArticulationType.ACCENT] }),
    b.chord('2', [b.note('D5', { tieTo: 'mel-tie-end' })]),
  ]
  const m4 = [
    b.chord('2', [b.note('D5', { id: 'mel-tie-end' })], { id: 'mel-last' }),
    b.rest('4'),
  ]

  const staff = {
    id: 'mel-staff',
    kind: 'staff' as const,
    staffSpanners: [],
    measures: [
      b.staffMeasure('mel-m0', [b.voice(b.seq('8:D4 8:G4'))], [{ tick: 0, clef: b.treble() }]),
      b.staffMeasure('mel-m1', [b.voice(m1)]),
      b.staffMeasure('mel-m2', [b.voice(m2)]),
      b.staffMeasure('mel-m3', [b.voice(m3)]),
      b.staffMeasure('mel-m4', [b.voice(m4)]),
    ],
  }

  return {
    title: 'Simple Melody',
    composer: 'Example',
    tpq,
    measures,
    globalSpanners: [],
    tracks: [{
      id: 'mel-flute',
      kind: 'track',
      instrumentName: 'Flute',
      instrumentAbbr: 'Fl.',
      staves: [staff],
      trackSpanners: [
        {
          id: 'mel-slur',
          kind: 'slur',
          start: { type: 'element', id: m1[0]!.id },
          end: { type: 'element', id: m1[2]!.id },
        },
        {
          id: 'mel-cresc',
          kind: 'hairpin',
          type: 'cresc',
          start: { type: 'tick', measureId: 'mel-m3', tick: 0 },
          end: { type: 'tick', measureId: 'mel-m3', tick: 4 * tpq },
        },
      ],
      attachments: [
        {
          id: 'mel-dyn-mp',
          kind: 'dynamic',
          type: DynamicType.MP,
          at: { type: 'element', id: staff.measures[0]!.voices[0]!.events[0]!.id },
        },
        {
          id: 'mel-dyn-f',
          kind: 'dynamic',
          type: DynamicType.F,
          at: { type: 'element', id: 'mel-last' },
        },
      ],
    }],
  }
}
