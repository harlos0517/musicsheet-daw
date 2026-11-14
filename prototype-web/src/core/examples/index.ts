import type MusicData from '../types/MusicData'
import { band } from './band'
import { melody } from './melody'
import { piano } from './piano'

export const examples: { name: string, build: () => MusicData }[] = [
  { name: 'Simple melody', build: melody },
  { name: 'Piano', build: piano },
  { name: 'Band (repeats, transposing, drums)', build: band },
]
