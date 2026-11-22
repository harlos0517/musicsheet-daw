#pragma once

// Moving notes up and down, the MuseScore way: Up/Down a semitone, Alt+Shift+Up/Down a
// step in the key, Ctrl+Up/Down an octave.

#include <string>
#include <vector>

#include "core/types/MusicData.h"

namespace msd
{

enum class PitchMove { SemitoneUp, SemitoneDown, StepUp, StepDown, OctaveUp, OctaveDown };

struct PitchChange
{
    NodeId noteId;
    Pitch before;
    Pitch after;
};

// One undo step.
struct Edit
{
    std::string name; // "Move pitch up", for the status line
    std::vector<PitchChange> changes;
};

// The changes moving `notes` makes, notes tied to them included; does not change `music`.
// A semitone is spelled as in the key when the key has that pitch, else as a natural,
// else with a sharp going up and a flat going down. A step goes to the next letter with
// the key's alteration for it. Tied notes get the pitch of the note moved. Unpitched
// notes, unknown IDs and moves past MIDI 0-127 are left out.
Edit movePitches(const MusicData& music, const std::vector<NodeId>& notes, PitchMove move);

// The measures an edit changes notes in, in score order, each once: the ones to lay
// out again.
std::vector<NodeId> editedMeasures(const MusicData& music, const Edit& edit);

// MIDI note number: C4 is 60.
int midiNumber(const Pitch& pitch);

} // namespace msd
