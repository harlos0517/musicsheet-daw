#pragma once

// Every pitched note of a score, grace notes included, with the key in effect where it
// is.

#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

#include "core/types/MusicData.h"

namespace msd
{

// Calls f(note, keyAccidentals, measureId) for every pitched note, in score order:
// track, staff, measure, voice, then time (a chord's grace notes before it). `Music` is MusicData or
// const MusicData, and the note is passed the same way.
template <class Music, class F>
void forEachNote(Music& music, F&& f);

// The notes by ID. Points into the MusicData, which must outlive it and keep its
// shape: changing pitches is fine, adding or removing notes is not.
class NoteIndex
{
public:
    struct Entry
    {
        const Note* note = nullptr;
        int keyAccidentals = 0;       // the key in effect in its measure
        std::vector<NodeId> tiedFrom; // notes tied to this one
    };

    explicit NoteIndex(const MusicData& music);

    // nullptr for an ID that is no pitched note (an unpitched one, a rest, a stale ID).
    const Entry* find(const NodeId& id) const;

    // The note and every note tied to it, either way along the ties.
    std::vector<NodeId> tieChain(const NodeId& id) const;

private:
    std::unordered_map<NodeId, Entry> notes;
};

// The measure a pitched note is in; "" for an ID that is no pitched note.
NodeId measureOfNote(const MusicData& music, const NodeId& noteId);

namespace detail
{

template <class Notes, class F>
void eachChordNote(Notes& notes, int key, const NodeId& measureId, F& f)
{
    for (auto& chordNote : notes)
        if (auto* note = std::get_if<Note>(&chordNote))
            f(*note, key, measureId);
}

template <class Event>
auto& asVariant(Event& event)
{
    using Variant = std::variant<Chord, Rest, Space, Tuplet>;
    if constexpr (std::is_const_v<Event>)
        return static_cast<const Variant&>(event);
    else
        return static_cast<Variant&>(event);
}

template <class Events, class F>
void eachEventNote(Events& events, int key, const NodeId& measureId, F& f)
{
    for (auto& event : events)
    {
        auto& variant = asVariant(event);
        if (auto* chord = std::get_if<Chord>(&variant))
        {
            if (chord->graceNotes)
                for (auto& grace : chord->graceNotes->chords)
                    eachChordNote(grace.notes, key, measureId, f);
            eachChordNote(chord->notes, key, measureId, f);
        }
        else if (auto* tuplet = std::get_if<Tuplet>(&variant))
            eachEventNote(tuplet->events, key, measureId, f);
    }
}

} // namespace detail

template <class Music, class F>
void forEachNote(Music& music, F&& f)
{
    // The key in effect in each measure: the last one written, C major before any.
    std::unordered_map<NodeId, int> keys;
    int key = 0;
    for (const Measure& measure : music.measures)
    {
        if (measure.key)
            key = measure.key->accidentals;
        keys[measure.id] = key;
    }
    for (auto& track : music.tracks)
        for (auto& staff : track.staves)
            for (auto& measure : staff.measures)
            {
                const auto found = keys.find(measure.measureId);
                const int measureKey = found == keys.end() ? 0 : found->second;
                for (auto& voice : measure.voices)
                    detail::eachEventNote(voice.events, measureKey, measure.measureId, f);
            }
}

} // namespace msd
