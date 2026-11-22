#pragma once

// Undo and redo.

#include <vector>

#include "core/types/MusicData.h"
#include "edit/PitchEdit.h"

namespace msd
{

class EditHistory
{
public:
    void apply(MusicData& music, Edit edit); // applies it, clears redo; an empty edit is dropped
    bool undo(MusicData& music);             // false when there's nothing to undo
    bool redo(MusicData& music);
    void clear();                            // for a newly opened score

    // The edit undo or redo would change, or nullptr.
    const Edit* nextUndo() const { return done.empty() ? nullptr : &done.back(); }
    const Edit* nextRedo() const { return undone.empty() ? nullptr : &undone.back(); }

private:
    std::vector<Edit> done, undone;
};

} // namespace msd
