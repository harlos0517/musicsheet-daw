/**
 * Runs a TypeScript script with the `@/` → `src/` alias, the same on Windows and POSIX:
 *   node src/scripts/run.mjs src/scripts/export-midi.ts
 */
import { resolve } from 'node:path'
import { pathToFileURL } from 'node:url'

import { createJiti } from 'jiti'

const [script] = process.argv.slice(2)
if (!script) {
  console.error('usage: node src/scripts/run.mjs <script.ts>')
  process.exit(1)
}

const jiti = createJiti(import.meta.url, {
  alias: { '@': resolve(import.meta.dirname, '..') },
})
await jiti.import(pathToFileURL(resolve(script)).href)
