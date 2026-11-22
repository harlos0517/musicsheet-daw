#include "edit/NoteIndex.h"

#include <unordered_set>

namespace msd
{

NoteIndex::NoteIndex(const MusicData& music)
{
    forEachNote(music, [&](const Note& note, int key, const NodeId&) {
        Entry& entry = notes[note.id];
        entry.note = &note;
        entry.keyAccidentals = key;
    });
    for (auto& [id, entry] : notes)
        if (entry.note->tie)
            if (const auto* targets = std::get_if<std::vector<TieTarget>>(&*entry.note->tie))
                for (const TieTarget& target : *targets)
                {
                    const auto to = notes.find(target.to);
                    if (to != notes.end())
                        to->second.tiedFrom.push_back(id);
                }
}

const NoteIndex::Entry* NoteIndex::find(const NodeId& id) const
{
    const auto found = notes.find(id);
    return found == notes.end() ? nullptr : &found->second;
}

std::vector<NodeId> NoteIndex::tieChain(const NodeId& id) const
{
    std::vector<NodeId> chain;
    std::unordered_set<NodeId> seen;
    std::vector<NodeId> todo { id };
    while (!todo.empty())
    {
        const NodeId next = std::move(todo.back());
        todo.pop_back();
        const Entry* entry = find(next);
        if (entry == nullptr || !seen.insert(next).second)
            continue;
        chain.push_back(next);
        if (entry->note->tie)
            if (const auto* targets = std::get_if<std::vector<TieTarget>>(&*entry->note->tie))
                for (const TieTarget& target : *targets)
                    todo.push_back(target.to);
        for (const NodeId& from : entry->tiedFrom)
            todo.push_back(from);
    }
    return chain;
}

} // namespace msd
