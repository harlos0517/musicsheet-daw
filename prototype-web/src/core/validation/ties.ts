import type { Note, UnpitchedNote } from '@/core/types/MusicData'

/**
 * Ids of the notes a note is tied to. More than one when the tie crosses a repeat:
 * the last note before a repeat sign can tie into the first and the second ending.
 */
export const tieTargets = (note: Note | UnpitchedNote): string[] =>
  Array.isArray(note.tie) ? note.tie.map(target => target.to) : []
