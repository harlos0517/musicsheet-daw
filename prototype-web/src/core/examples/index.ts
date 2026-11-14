import type MusicData from '../types/MusicData'
import { melody } from './melody'

export const examples: { name: string, build: () => MusicData }[] = [
  { name: 'Simple melody', build: melody },
]
