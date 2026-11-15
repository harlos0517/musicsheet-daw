import type MusicData from '@/core/types/MusicData'
import { BarlineStyle, DynamicType } from '@/core/types/MusicData'
import { ScoreBuilder } from './build'

/**
 * Piano in F major, 3/4: two staves, two voices with a spacer, a mid-bar clef change,
 * a cross-staff slur, an 8va line, grace notes, pedal, a dynamic between the staves,
 * and a full-measure rest.
 */
export const piano = (): MusicData => {
  const b = new ScoreBuilder('pno')

  const measures = [
    b.measure('pno-m1', {
      timeSig: b.timeSig(3, 4),
      key: b.key(-1),
      tempos: [b.tempo('4', 72, { text: 'Andante' })],
    }),
    b.measure('pno-m2'),
    b.measure('pno-m3', { barline: BarlineStyle.FINAL }),
  ]

  const rh1Upper = b.seq('4:C5 4:D5 4:E5')
  const rh1Lower = [b.space('4'), ...b.seq('2:A4')]
  const rh2 = [
    b.chord('4', ['F5'], {
      id: 'pno-8va-start',
      grace: b.grace([b.chord('8', ['G5'])], { slash: true }),
    }),
    ...b.seq('8:E5 8:D5'),
    b.chord('4', ['C5'], { id: 'pno-slur-start' }),
  ]
  const lh1 = b.seq('2.:F3,C4')
  const lh2 = b.seq('4:F3 4:A3 4:C4')
  const lh3 = [b.chord('2.', ['F2', 'C3', 'F3'], { id: 'pno-slur-end' })]

  const upper = {
    id: 'pno-rh',
    kind: 'staff' as const,
    staffSpanners: [{
      id: 'pno-8va',
      kind: 'ottava' as const,
      shift: 1 as const,
      start: { type: 'element' as const, id: 'pno-8va-start' },
      end: { type: 'element' as const, id: rh2[2]!.id },
    }],
    measures: [
      b.staffMeasure(
        'pno-m1',
        [b.voice(rh1Upper), b.voice(rh1Lower)],
        [{ tick: 0, clef: b.treble() }],
      ),
      b.staffMeasure('pno-m2', [b.voice(rh2)]),
      b.staffMeasure('pno-m3', [b.voice(b.rest('2.', { fullMeasure: true }))]),
    ],
  }
  const lower = {
    id: 'pno-lh',
    kind: 'staff' as const,
    staffSpanners: [],
    measures: [
      b.staffMeasure('pno-m1', [b.voice(lh1)], [{ tick: 0, clef: b.bass() }]),
      b.staffMeasure('pno-m2', [b.voice(lh2)], [{ tick: 1920, clef: b.treble() }]),
      b.staffMeasure('pno-m3', [b.voice(lh3)], [{ tick: 0, clef: b.bass() }]),
    ],
  }

  return {
    title: 'Piano Study',
    composer: 'Example',
    tpq: 960,
    measures,
    globalSpanners: [],
    tracks: [{
      id: 'pno',
      kind: 'track',
      instrumentName: 'Piano',
      instrumentAbbr: 'Pno.',
      staves: [upper, lower],
      trackSpanners: [
        {
          id: 'pno-slur',
          kind: 'slur',
          start: { type: 'element', id: 'pno-slur-start' },
          end: { type: 'element', id: 'pno-slur-end' },
        },
        {
          id: 'pno-ped',
          kind: 'pedal',
          type: 'sustain',
          start: { type: 'tick', measureId: 'pno-m1', tick: 0 },
          end: { type: 'tick', measureId: 'pno-m2', tick: 2880 },
        },
        {
          id: 'pno-cresc',
          kind: 'hairpin',
          type: 'cresc',
          start: { type: 'element', id: rh1Upper[0]!.id },
          end: { type: 'element', id: rh1Upper[2]!.id },
          includeEnd: true,
        },
      ],
      attachments: [
        {
          id: 'pno-dyn-p',
          kind: 'dynamic',
          type: DynamicType.P,
          at: { type: 'tick', measureId: 'pno-m1', tick: 0 },
        },
        {
          id: 'pno-dyn-mf',
          kind: 'dynamic',
          type: DynamicType.MF,
          at: { type: 'element', id: 'pno-8va-start' },
        },
      ],
    }],
  }
}
