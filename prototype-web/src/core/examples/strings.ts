import type MusicData from '@/core/types/MusicData'
import { ArticulationType, BarlineStyle, DynamicType } from '@/core/types/MusicData'
import { ScoreBuilder } from './build'

/**
 * Solo violin, 4/4, written for playback rather than for looks.
 * Covers what the other examples leave out: fp and sfz, pizz. / arco markings,
 * a one-note technique with a fallback, a custom line that becomes a curve,
 * and a hairpin al niente.
 */
export const strings = (): MusicData => {
  const b = new ScoreBuilder('vln')
  const tpq = 960

  const measures = [
    b.measure('vln-m1', {
      timeSig: b.timeSig(4, 4),
      key: b.key(0),
      tempos: [b.tempo('4', 80)],
    }),
    b.measure('vln-m2'),
    b.measure('vln-m3'),
    b.measure('vln-m4', { barline: BarlineStyle.FINAL }),
  ]

  const m1 = [b.chord('1', ['G4'], { id: 'vln-fp' })]
  const m2 = [
    b.chord('4', ['A4'], { id: 'vln-pizz' }),
    ...b.seq('4:B4'),
    b.chord('4', ['C5'], { id: 'vln-accent', articulations: [ArticulationType.ACCENT] }),
    ...b.seq('4:D5'),
  ]
  const m3 = [
    b.chord('2', ['E5'], { id: 'vln-arco' }),
    b.chord('2', ['D5'], { id: 'vln-sfz' }),
  ]
  const m4 = [b.chord('1', [b.note('G4', { id: 'vln-last-note' })], { id: 'vln-last' })]

  const staff = {
    id: 'vln-staff',
    kind: 'staff' as const,
    staffSpanners: [],
    measures: [
      b.staffMeasure('vln-m1', [b.voice(m1)], [{ tick: 0, clef: b.treble() }]),
      b.staffMeasure('vln-m2', [b.voice(m2)]),
      b.staffMeasure('vln-m3', [b.voice(m3)]),
      b.staffMeasure('vln-m4', [b.voice(m4)]),
    ],
  }

  return {
    title: 'Violin Study',
    composer: 'Example',
    tpq,
    measures,
    globalSpanners: [],
    tracks: [{
      id: 'vln',
      kind: 'track',
      instrumentName: 'Violin',
      instrumentAbbr: 'Vln.',
      staves: [staff],
      trackSpanners: [
        {
          id: 'vln-slur',
          kind: 'slur',
          start: { type: 'element', id: 'vln-arco' },
          end: { type: 'element', id: 'vln-sfz' },
        },
        {
          id: 'vln-vib',
          kind: 'custom-spanner',
          techniqueId: 'user:molto-vibrato',
          text: 'molto vib.',
          start: { type: 'tick', measureId: 'vln-m3', tick: 0 },
          end: { type: 'tick', measureId: 'vln-m3', tick: 4 * tpq },
        },
        {
          id: 'vln-dim',
          kind: 'hairpin',
          type: 'dim',
          niente: true,
          start: { type: 'tick', measureId: 'vln-m4', tick: 0 },
          end: { type: 'tick', measureId: 'vln-m4', tick: 4 * tpq },
        },
      ],
      attachments: [
        {
          id: 'vln-dyn-fp',
          kind: 'dynamic',
          type: DynamicType.FP,
          at: { type: 'element', id: 'vln-fp' },
        },
        {
          id: 'vln-tech-pizz',
          kind: 'technique',
          techniqueId: 'core:pizz',
          text: 'pizz.',
          scope: 'until-next',
          at: { type: 'element', id: 'vln-pizz' },
        },
        {
          id: 'vln-tech-arco',
          kind: 'technique',
          techniqueId: 'core:arco',
          text: 'arco',
          scope: 'until-next',
          at: { type: 'element', id: 'vln-arco' },
        },
        {
          id: 'vln-dyn-sfz',
          kind: 'dynamic',
          type: DynamicType.SFZ,
          at: { type: 'element', id: 'vln-sfz' },
        },
        {
          id: 'vln-tech-tasto',
          kind: 'technique',
          techniqueId: 'user:sul-tasto',
          text: 'sul tasto',
          scope: 'this',
          at: { type: 'element', id: 'vln-last' },
        },
      ],
    }],
  }
}
