import type InterpretationProfile from '@/core/types/InterpretationProfile'
import type { ChangeMode } from '@/core/types/InterpretationProfile'

/** Dynamic level before the first dynamic marking, when the profile does not set one. */
export const DEFAULT_INITIAL_DYNAMICS = 0.5

/**
 * Grace note timing when the profile does not set it: an appoggiatura takes half its
 * written length out of the main note, an acciaccatura a quarter of it before the beat.
 */
export const DEFAULT_GRACE_NOTES: NonNullable<InterpretationProfile['graceNotes']> = {
  appoggiatura: { ratio: 0.5 },
  acciaccatura: { ratio: 0.25 },
}

export const applyMode = (mode: ChangeMode, current: number, value: number): number => {
  if (mode === 'set') return value
  return mode === 'add' ? current + value : current * value
}

const mergeById = <T extends { id: string }>(parent: T[], child: T[]): T[] => {
  const result = [...parent]
  for (const item of child) {
    const at = result.findIndex(existing => existing.id === item.id)
    if (at >= 0) result[at] = item
    else result.push(item)
  }
  return result
}

/**
 * Flattens an `extends` chain into one profile: a child's dynamics replace the parent's
 * by type, its rules replace the parent's by id (keeping the parent's position), and
 * rules switched off with `enabled: false` are dropped.
 */
export const resolveInterpretationProfile = (
  profiles: InterpretationProfile[],
  id: string,
): InterpretationProfile => {
  const chain: InterpretationProfile[] = []
  const seen = new Set<string>()
  let next: string | undefined = id
  while (next !== undefined && !seen.has(next)) {
    seen.add(next)
    const profile = profiles.find(p => p.id === next)
    if (!profile) throw new Error(`unknown interpretation profile "${next}"`)
    chain.unshift(profile)
    next = profile.extends
  }

  let resolved: InterpretationProfile = { ...chain[0]!, id }
  for (const profile of chain.slice(1)) {
    const dynamics = [...(resolved.dynamics ?? [])]
    for (const mapping of profile.dynamics ?? []) {
      const at = dynamics.findIndex(d => d.type === mapping.type)
      if (at >= 0) dynamics[at] = mapping
      else dynamics.push(mapping)
    }
    const hairpin = profile.hairpin ?? resolved.hairpin
    const initialDynamics = profile.initialDynamics ?? resolved.initialDynamics
    const graceNotes = profile.graceNotes ?? resolved.graceNotes
    const defaultPhrasing = profile.defaultPhrasing ?? resolved.defaultPhrasing
    const swell = profile.swell ?? resolved.swell
    const expandTremolo = profile.expandTremolo ?? resolved.expandTremolo
    resolved = {
      id,
      name: profile.name,
      dynamics,
      ...(initialDynamics !== undefined ? { initialDynamics } : {}),
      ...(graceNotes ? { graceNotes } : {}),
      ...(defaultPhrasing ? { defaultPhrasing } : {}),
      ...(swell ? { swell } : {}),
      ...(expandTremolo ? { expandTremolo } : {}),
      ...(hairpin ? { hairpin } : {}),
      noteRules: mergeById(resolved.noteRules, profile.noteRules),
      curveRules: mergeById(resolved.curveRules, profile.curveRules),
    }
  }
  return {
    ...resolved,
    noteRules: resolved.noteRules.filter(rule => rule.enabled !== false),
    curveRules: resolved.curveRules.filter(rule => rule.enabled !== false),
  }
}
