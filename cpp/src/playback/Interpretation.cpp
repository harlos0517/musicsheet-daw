#include "playback/Interpretation.h"

#include <algorithm>
#include <optional>
#include <set>
#include <stdexcept>

namespace msd
{
namespace
{

// A child's item replaces the parent's with the same ID, in its place; new ones go last.
template <class T>
std::vector<T> mergeById(std::vector<T> result, const std::vector<T>& child)
{
    for (const T& item : child)
    {
        const auto at =
            std::find_if(result.begin(), result.end(), [&](const T& existing) { return existing.id == item.id; });
        if (at != result.end())
            *at = item;
        else
            result.push_back(item);
    }
    return result;
}

template <class T>
std::vector<T> enabledOnly(std::vector<T> rules)
{
    std::erase_if(rules, [](const T& rule) { return rule.enabled == false; });
    return rules;
}

} // namespace

double applyMode(ChangeMode mode, double current, double value)
{
    if (mode == ChangeMode::Set)
        return value;
    return mode == ChangeMode::Add ? current + value : current * value;
}

InterpretationProfile resolveInterpretationProfile(const std::vector<InterpretationProfile>& profiles,
                                                   const std::string& id)
{
    std::vector<const InterpretationProfile*> chain; // root first
    std::set<std::string> seen;
    std::optional<std::string> next = id;
    while (next && !seen.contains(*next))
    {
        seen.insert(*next);
        const auto profile = std::find_if(profiles.begin(), profiles.end(),
                                          [&](const InterpretationProfile& p) { return p.id == *next; });
        if (profile == profiles.end())
            throw std::runtime_error("unknown interpretation profile \"" + *next + "\"");
        chain.insert(chain.begin(), &*profile);
        next = profile->extends;
    }

    InterpretationProfile resolved = *chain.front();
    resolved.id = id;
    for (std::size_t i = 1; i < chain.size(); ++i)
    {
        const InterpretationProfile& profile = *chain[i];
        std::vector<DynamicMapping> dynamics = resolved.dynamics.value_or(std::vector<DynamicMapping> {});
        for (const DynamicMapping& mapping : profile.dynamics.value_or(std::vector<DynamicMapping> {}))
        {
            const auto at = std::find_if(dynamics.begin(), dynamics.end(),
                                         [&](const DynamicMapping& d) { return d.type == mapping.type; });
            if (at != dynamics.end())
                *at = mapping;
            else
                dynamics.push_back(mapping);
        }
        InterpretationProfile merged {
            .id = id,
            .name = profile.name,
            .initialDynamics = profile.initialDynamics ? profile.initialDynamics : resolved.initialDynamics,
            .defaultPhrasing = profile.defaultPhrasing ? profile.defaultPhrasing : resolved.defaultPhrasing,
            .swell = profile.swell ? profile.swell : resolved.swell,
            .dynamics = std::move(dynamics),
            .hairpin = profile.hairpin ? profile.hairpin : resolved.hairpin,
            .expandTremolo = profile.expandTremolo ? profile.expandTremolo : resolved.expandTremolo,
            .graceNotes = profile.graceNotes ? profile.graceNotes : resolved.graceNotes,
            .noteRules = mergeById(resolved.noteRules, profile.noteRules),
            .curveRules = mergeById(resolved.curveRules, profile.curveRules),
        };
        resolved = std::move(merged);
    }
    resolved.noteRules = enabledOnly(std::move(resolved.noteRules));
    resolved.curveRules = enabledOnly(std::move(resolved.curveRules));
    return resolved;
}

} // namespace msd
