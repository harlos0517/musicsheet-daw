#include "edit/PitchEdit.h"

#include <optional>
#include <unordered_set>

#include "edit/NoteIndex.h"
#include "layout/Accidentals.h"

namespace msd
{

namespace
{

constexpr int semitones[] = { 0, 2, 4, 5, 7, 9, 11 }; // C D E F G A B

int stepIndex(Step step) { return static_cast<int>(step); }

// `midi` spelled on `step` with a flat, a natural or a sharp, if one of them gives it.
std::optional<Pitch> spellOn(int midi, Step step)
{
    for (int alter = -1; alter <= 1; ++alter)
    {
        const int pitchClass = semitones[stepIndex(step)] + alter; // -1 for C flat, 12 for B sharp
        if (((midi - pitchClass) % 12 + 12) % 12 == 0)
            return Pitch { step, alter, (midi - pitchClass) / 12 - 1 };
    }
    return std::nullopt;
}

Pitch spellSemitone(int midi, int key, bool up)
{
    std::optional<Pitch> natural, raised, lowered;
    for (int s = 0; s < 7; ++s)
    {
        const std::optional<Pitch> pitch = spellOn(midi, static_cast<Step>(s));
        if (!pitch)
            continue;
        if (pitch->alter == keyAlter(pitch->step, key))
            return *pitch;
        if (pitch->alter == 0)
            natural = pitch;
        else if (pitch->alter > 0)
            raised = pitch;
        else
            lowered = pitch;
    }
    if (natural)
        return *natural;
    return up ? *raised : *lowered; // a pitch with no natural has both a sharp and a flat
}

std::optional<Pitch> moved(const Pitch& pitch, int key, PitchMove move)
{
    Pitch result = pitch;
    switch (move)
    {
    case PitchMove::SemitoneUp:
    case PitchMove::SemitoneDown: {
        const bool up = move == PitchMove::SemitoneUp;
        result = spellSemitone(midiNumber(pitch) + (up ? 1 : -1), key, up);
        break;
    }
    case PitchMove::StepUp:
    case PitchMove::StepDown: {
        const int index = pitch.octave * 7 + stepIndex(pitch.step) + (move == PitchMove::StepUp ? 1 : -1);
        const int octave = index >= 0 ? index / 7 : (index - 6) / 7;
        result.step = static_cast<Step>(index - octave * 7);
        result.octave = octave;
        result.alter = keyAlter(result.step, key);
        break;
    }
    case PitchMove::OctaveUp: result.octave += 1; break;
    case PitchMove::OctaveDown: result.octave -= 1; break;
    }
    const int midi = midiNumber(result);
    if (midi < 0 || midi > 127)
        return std::nullopt;
    return result;
}

const char* moveName(PitchMove move)
{
    switch (move)
    {
    case PitchMove::SemitoneUp: return "Move pitch up";
    case PitchMove::SemitoneDown: return "Move pitch down";
    case PitchMove::StepUp: return "Move pitch up a step";
    case PitchMove::StepDown: return "Move pitch down a step";
    case PitchMove::OctaveUp: return "Move pitch up an octave";
    case PitchMove::OctaveDown: return "Move pitch down an octave";
    }
    return "Move pitch";
}

bool samePitch(const Pitch& a, const Pitch& b)
{
    return a.step == b.step && a.alter == b.alter && a.octave == b.octave;
}

} // namespace

int midiNumber(const Pitch& pitch) { return (pitch.octave + 1) * 12 + semitones[stepIndex(pitch.step)] + pitch.alter; }

std::vector<NodeId> editedMeasures(const MusicData& music, const Edit& edit)
{
    std::unordered_set<NodeId> notes;
    for (const PitchChange& change : edit.changes)
        notes.insert(change.noteId);
    std::vector<NodeId> measures;
    std::unordered_set<NodeId> seen;
    forEachNote(music, [&](const Note& note, int, const NodeId& measureId) {
        if (notes.contains(note.id) && seen.insert(measureId).second)
            measures.push_back(measureId);
    });
    return measures;
}

Edit movePitches(const MusicData& music, const std::vector<NodeId>& notes, PitchMove move)
{
    Edit edit { .name = moveName(move), .changes = {} };
    const NoteIndex index(music);
    std::unordered_set<NodeId> done; // a tie chain moves once, however many of it are selected
    for (const NodeId& id : notes)
    {
        const NoteIndex::Entry* entry = index.find(id);
        if (entry == nullptr || done.contains(id))
            continue;
        const std::vector<NodeId> chain = index.tieChain(id);
        done.insert(chain.begin(), chain.end());
        const std::optional<Pitch> after = moved(entry->note->pitch, entry->keyAccidentals, move);
        if (!after)
            continue;
        for (const NodeId& tied : chain)
        {
            const Pitch& before = index.find(tied)->note->pitch;
            if (!samePitch(before, *after))
                edit.changes.push_back({ tied, before, *after });
        }
    }
    return edit;
}

} // namespace msd
