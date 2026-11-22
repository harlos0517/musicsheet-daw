/**
 * Writes each example score as a MIDI file, plus its MusicData (`.music.json`, read by
 * the C++ app), the PlaybackSetup it was played with (`.setup.json`), the played order
 * of the measures and the tempo map (`.timing.json`), and the MusicEvents and MIDI
 * messages behind it as JSON, into `midi-out/`. MusicXML files in `scores/` are
 * imported, checked and written too, each with its setup, timing and a report of what
 * the import left out. Run with `pnpm midi`.
 */
import { existsSync, mkdirSync, readdirSync, readFileSync, writeFileSync } from 'node:fs'
import { basename, resolve } from 'node:path'

import { cinematicSetup } from '@/core/examples/cinematicStudio'
import { importMusicXml } from '@/core/musicxml/import'
import { validate } from '@/core/validation/validate'
import { band } from '@/core/examples/band'
import { melody } from '@/core/examples/melody'
import { piano } from '@/core/examples/piano'
import { setups } from '@/core/examples/profiles'
import { strings } from '@/core/examples/strings'
import type { Playback } from '@/core/playback'
import { play } from '@/core/playback'
import type MusicData from '@/core/types/MusicData'
import type { PlaybackSetup } from '@/core/types/PlaybackSetup'

const scores = { melody, piano, band, strings }
const out = resolve(import.meta.dirname, '../../midi-out')
mkdirSync(out, { recursive: true })

const writeMusic = (name: string, music: MusicData) =>
  writeFileSync(resolve(out, `${name}.music.json`), JSON.stringify(music, null, 2))
const writeSetup = (name: string, setup: PlaybackSetup) =>
  writeFileSync(resolve(out, `${name}.setup.json`), JSON.stringify(setup, null, 2))
/** The order measures are played in, and the tempo map: the first steps of playback. */
const writeTiming = (name: string, result: Playback) => {
  const timing = {
    played: result.played.map(({ info, pass, startTick }) =>
      ({ measureIndex: info.index, measureId: info.measure.id, pass, startTick })),
    tempo: result.tempo.points,
  }
  writeFileSync(resolve(out, `${name}.timing.json`), JSON.stringify(timing, null, 2))
}

for (const [name, build] of Object.entries(scores)) {
  const music = build()
  writeMusic(name, music)
  const setup = setups[name as keyof typeof scores]
  writeSetup(name, setup)
  const result = play(music, setup)
  writeTiming(name, result)
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

const scoreFolder = resolve(import.meta.dirname, '../../scores')
const files = existsSync(scoreFolder)
  ? readdirSync(scoreFolder).filter(file => /\.(musicxml|xml)$/i.test(file))
  : []
for (const file of files) {
  const name = basename(file).replace(/\.[^.]+$/, '')
  const source = readFileSync(resolve(scoreFolder, file), 'utf8')
  const { music, kitNotes, skipped } = importMusicXml(source)
  writeMusic(name, music)
  const problems = validate(music)
  const setup = cinematicSetup(music, kitNotes)
  writeSetup(name, setup)
  const result = play(music, setup)
  writeTiming(name, result)
  writeFileSync(resolve(out, `${name}.mid`), result.file)

  const count = (items: string[]) => {
    const counts = new Map<string, number>()
    for (const item of items) counts.set(item, (counts.get(item) ?? 0) + 1)
    return [...counts].sort((a, b) => b[1] - a[1])
      .map(([item, n]) => `${String(n).padStart(6)}  ${item}`)
  }
  const notes = result.events.filter(event => event.kind === 'note-event').length
  const report = [
    `${music.title} (${file})`,
    `${music.measures.length} measures, ${music.tracks.length} parts, ${notes} notes,`
    + ` tpq ${music.tpq}`,
    '',
    'Parts, and the DAW tracks (outputs) and profiles each one plays on:',
    ...music.tracks.flatMap(track => (setup.trackProfiles[track.id]?.outputs ?? [])
      .map((output, i) => {
        const profile = setup.instrumentProfiles.find(p => p.id === output.instrumentProfileId)
        const part = i === 0 ? track.instrumentName : ''
        const name = output.name && output.name !== track.instrumentName ? `→ ${output.name}: ` : ''
        const keys = output.sounds
          ? `  (${Object.entries(output.sounds).map(([from, to]) => `${from} → ${to}`).join(', ')})`
          : ''
        return `  ${part.padEnd(28)} ${name}${profile?.name ?? '?'}${keys}`
      })),
    '',
    'Left out or approximated by the import:',
    ...Object.entries(skipped).sort((a, b) => b[1] - a[1])
      .map(([what, n]) => `${String(n).padStart(6)}  ${what}`),
    '',
    `Validation problems: ${problems.length}`,
    ...count(problems.map(p => `${p.severity} ${p.code}`)),
    ...problems.slice(0, 20).map(p => `  ${p.code}: ${p.message} (${p.path})`),
    '',
    `Playback warnings: ${result.warnings.length}`,
    ...count(result.warnings.map(w => w.replace(/^note \S+: /, ''))),
  ]
  writeFileSync(resolve(out, `${name}.report.txt`), `${report.join('\n')}\n`)
  const messages = result.tracks.reduce((sum, track) => sum + track.messages.length, 0)
  console.log(
    `${name}: ${music.tracks.length} parts, ${result.played.length} measures, ${notes} notes,`,
    `${messages} MIDI messages, ${problems.length} validation problems`,
  )
}
console.log(`written to ${out}`)
