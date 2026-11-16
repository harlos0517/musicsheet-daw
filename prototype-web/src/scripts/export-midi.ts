/**
 * Writes each example score as a MIDI file, plus the MusicEvents and MIDI messages
 * behind it as JSON, into `midi-out/`. Run with `pnpm midi`.
 */
import { mkdirSync, writeFileSync } from 'node:fs'
import { resolve } from 'node:path'

import { band } from '@/core/examples/band'
import { melody } from '@/core/examples/melody'
import { piano } from '@/core/examples/piano'
import { setups } from '@/core/examples/profiles'
import { strings } from '@/core/examples/strings'
import { play } from '@/core/playback'

const scores = { melody, piano, band, strings }
const out = resolve(import.meta.dirname, '../../midi-out')
mkdirSync(out, { recursive: true })

for (const [name, build] of Object.entries(scores)) {
  const result = play(build(), setups[name as keyof typeof scores])
  writeFileSync(resolve(out, `${name}.mid`), result.file)
  writeFileSync(resolve(out, `${name}.events.json`), JSON.stringify(result.events, null, 2))
  writeFileSync(resolve(out, `${name}.midi.json`), JSON.stringify(result.tracks, null, 2))
  const notes = result.events.filter(event => event.kind === 'note-event').length
  const messages = result.tracks.reduce((sum, track) => sum + track.messages.length, 0)
  console.log(
    `${name}: ${result.played.length} measures played, ${notes} notes,`,
    `${result.events.length - notes} curve segments, ${messages} MIDI messages`,
  )
  for (const warning of result.warnings) console.log(`  warning: ${warning}`)
}
console.log(`written to ${out}`)
