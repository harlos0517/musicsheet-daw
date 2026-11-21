/**
 * Writes a MusicXML score as MIDI for visual effects (`examples/visual.ts`): dynamics on
 * velocity and CC1, tremolos written out. Run with `pnpm midi:visual scores/<file>`;
 * writes `midi-out/<name>.visual.mid`.
 */
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { basename, resolve } from 'node:path'

import { visualSetup } from '@/core/examples/visual'
import { importMusicXml } from '@/core/musicxml/import'
import { play } from '@/core/playback'

const [file] = process.argv.slice(3)
if (!file) {
  console.error('usage: pnpm midi:visual <score.musicxml>')
  process.exit(1)
}
const name = basename(file).replace(/\.[^.]+$/, '')
const { music, kitNotes } = importMusicXml(readFileSync(resolve(file), 'utf8'))
const result = play(music, visualSetup(music, kitNotes))

const out = resolve(import.meta.dirname, '../../midi-out')
mkdirSync(out, { recursive: true })
const path = resolve(out, `${name}.visual.mid`)
writeFileSync(path, result.file)

const notes = result.events.filter(event => event.kind === 'note-event').length
console.log(
  `${name}: ${music.tracks.length} parts, ${notes} notes, ${result.warnings.length} warnings`,
)
const counts = new Map<string, number>()
for (const warning of result.warnings) {
  const kind = warning.replace(/^note \S+: /, '')
  counts.set(kind, (counts.get(kind) ?? 0) + 1)
}
for (const [kind, n] of counts) console.log(`  ${String(n).padStart(5)}  ${kind}`)
console.log(`written to ${path}`)
