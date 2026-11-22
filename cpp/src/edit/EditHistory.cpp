#include "edit/EditHistory.h"

#include <unordered_map>

#include "edit/NoteIndex.h"

namespace msd
{

namespace
{

void setPitches(MusicData& music, const Edit& edit, bool forward)
{
    std::unordered_map<NodeId, const Pitch*> pitches;
    for (const PitchChange& change : edit.changes)
        pitches[change.noteId] = forward ? &change.after : &change.before;
    forEachNote(music, [&](Note& note, int, const NodeId&) {
        const auto found = pitches.find(note.id);
        if (found != pitches.end())
            note.pitch = *found->second;
    });
}

} // namespace

void EditHistory::apply(MusicData& music, Edit edit)
{
    if (edit.changes.empty())
        return;
    setPitches(music, edit, true);
    done.push_back(std::move(edit));
    undone.clear();
}

bool EditHistory::undo(MusicData& music)
{
    if (done.empty())
        return false;
    setPitches(music, done.back(), false);
    undone.push_back(std::move(done.back()));
    done.pop_back();
    return true;
}

bool EditHistory::redo(MusicData& music)
{
    if (undone.empty())
        return false;
    setPitches(music, undone.back(), true);
    done.push_back(std::move(undone.back()));
    undone.pop_back();
    return true;
}

void EditHistory::clear()
{
    done.clear();
    undone.clear();
}

} // namespace msd
