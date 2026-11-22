// Selecting and moving notes, with undo.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "edit/EditHistory.h"
#include "edit/NoteIndex.h"
#include "edit/PitchEdit.h"

using namespace msd;

// In msd, where Catch's comparisons find it.
namespace msd
{
static bool operator==(const Pitch& a, const Pitch& b)
{
    return a.step == b.step && a.alter == b.alter && a.octave == b.octave;
}
} // namespace msd

namespace
{

Note note(const std::string& id, Step step, int alter, int octave)
{
    return Note { .id = id, .pitch = { step, alter, octave }, .tie = {}, .accidentalDisplay = {} };
}

Chord chord(const std::string& id, std::vector<ChordNote> notes)
{
    return Chord {
        .id = id, .duration = {}, .notes = std::move(notes), .articulations = {}, .graceNotes = {}, .beam = {}
    };
}

// One track, one staff; a measure per entry of `measures`, each with its key and chords.
struct MeasureSpec
{
    int key = 0;
    std::vector<VoiceEvent> events;
};

MusicData score(std::vector<MeasureSpec> measures)
{
    MusicData music;
    Track track { .id = "t" };
    Staff staff { .id = "s" };
    for (std::size_t i = 0; i < measures.size(); ++i)
    {
        const std::string id = "m" + std::to_string(i + 1);
        music.measures.push_back(
            Measure { .id = id, .key = KeySignature { .id = id + "k", .accidentals = measures[i].key } });
        staff.measures.push_back(
            StaffMeasure { .id = "s" + id,
                           .measureId = id,
                           .voices = { Voice { .id = id + "v", .events = std::move(measures[i].events) } } });
    }
    track.staves.push_back(std::move(staff));
    music.tracks.push_back(std::move(track));
    return music;
}

MusicData oneNote(Step step, int alter, int octave, int key = 0)
{
    return score({ { key, { chord("c", { note("n", step, alter, octave) }) } } });
}

// The pitch `move` gives a single note, as "F#4"; "" when it doesn't move.
std::string after(MusicData music, PitchMove move)
{
    const Edit edit = movePitches(music, { "n" }, move);
    if (edit.changes.empty())
        return "";
    const Pitch& p = edit.changes[0].after;
    const std::string accidental = p.alter == 1    ? "#"
                                   : p.alter == -1 ? "b"
                                   : p.alter == 2  ? "x"
                                   : p.alter == -2 ? "bb"
                                                   : "";
    return std::string(1, "CDEFGAB"[static_cast<int>(p.step)]) + accidental + std::to_string(p.octave);
}

Pitch pitchOf(const MusicData& music, const std::string& id) { return NoteIndex(music).find(id)->note->pitch; }

} // namespace

TEST_CASE("a semitone up or down is spelled as in the key, else natural, else sharp up and flat down", "[edit]")
{
    CHECK(after(oneNote(Step::E, 0, 4), PitchMove::SemitoneUp) == "F4");
    CHECK(after(oneNote(Step::F, 0, 4), PitchMove::SemitoneUp) == "F#4");
    CHECK(after(oneNote(Step::F, 0, 4), PitchMove::SemitoneDown) == "E4");
    CHECK(after(oneNote(Step::E, 0, 4), PitchMove::SemitoneDown) == "Eb4");
    CHECK(after(oneNote(Step::B, 0, 3), PitchMove::SemitoneUp) == "C4");
    CHECK(after(oneNote(Step::C, 0, 4), PitchMove::SemitoneDown) == "B3");
    // In the key: D major has F sharp and C sharp, F major has B flat.
    CHECK(after(oneNote(Step::G, 0, 4, 2), PitchMove::SemitoneDown) == "F#4");
    CHECK(after(oneNote(Step::D, 0, 5, 2), PitchMove::SemitoneDown) == "C#5");
    CHECK(after(oneNote(Step::A, 0, 4, -1), PitchMove::SemitoneUp) == "Bb4");
    // Out of the key, a natural before an accidental: B natural in F major.
    CHECK(after(oneNote(Step::B, -1, 4, -1), PitchMove::SemitoneUp) == "B4");
    CHECK(after(oneNote(Step::C, 0, 5, -1), PitchMove::SemitoneDown) == "B4");
}

TEST_CASE("a step goes to the next letter with the key's alteration", "[edit]")
{
    CHECK(after(oneNote(Step::B, 0, 4, 2), PitchMove::StepUp) == "C#5");
    CHECK(after(oneNote(Step::C, 1, 5, 2), PitchMove::StepDown) == "B4");
    CHECK(after(oneNote(Step::C, 0, 4, -1), PitchMove::StepDown) == "Bb3");
    // An accidental out of the key goes: G sharp in C major steps up to A.
    CHECK(after(oneNote(Step::G, 1, 4), PitchMove::StepUp) == "A4");
}

TEST_CASE("an octave keeps the spelling", "[edit]")
{
    CHECK(after(oneNote(Step::F, 1, 4), PitchMove::OctaveUp) == "F#5");
    CHECK(after(oneNote(Step::B, -1, 4), PitchMove::OctaveDown) == "Bb3");
}

TEST_CASE("a note stays inside MIDI 0-127", "[edit]")
{
    CHECK(after(oneNote(Step::G, 0, 9), PitchMove::SemitoneUp) == "");  // G9 is 127
    CHECK(after(oneNote(Step::C, 0, -1), PitchMove::StepDown) == ""); // C-1 is 0
    CHECK(after(oneNote(Step::C, 0, 9), PitchMove::OctaveUp) == "");
}

TEST_CASE("the key is the one in effect in the note's measure", "[edit]")
{
    MusicData music = score({ { 2, {} }, { 0, { chord("c", { note("n", Step::G, 0, 4) }) } } });
    music.measures[1].key.reset(); // D major carries on
    CHECK(after(music, PitchMove::SemitoneDown) == "F#4");
}

TEST_CASE("tied notes move with the note, either way along the tie", "[edit]")
{
    Note first = note("a", Step::C, 0, 4);
    first.tie = std::vector<TieTarget> { { "b" } };
    Note second = note("b", Step::C, 0, 4);
    second.tie = std::vector<TieTarget> { { "c" } };
    MusicData music = score({ { 0, { chord("c1", { first }) } },
                              { 0, { chord("c2", { second }) } },
                              { 0, { chord("c3", { note("c", Step::C, 0, 4), note("other", Step::E, 0, 4) }) } } });

    const Edit fromEnd = movePitches(music, { "c" }, PitchMove::SemitoneUp);
    CHECK(fromEnd.changes.size() == 3);
    // Selecting two of the chain moves it once.
    const Edit both = movePitches(music, { "a", "b" }, PitchMove::SemitoneUp);
    REQUIRE(both.changes.size() == 3);
    for (const PitchChange& change : both.changes)
        CHECK(change.after == Pitch { Step::C, 1, 4 });
}

TEST_CASE("grace notes and notes in tuplets can be moved", "[edit]")
{
    Chord main = chord("main", { note("n", Step::D, 0, 4) });
    main.graceNotes =
        GraceChords { .id = "g", .chords = { GraceChord { .id = "gc", .notes = { note("grace", Step::C, 0, 4) } } } };
    Tuplet tuplet { .id = "tu", .events = { chord("tc", { note("inTuplet", Step::A, 0, 4) }) } };
    MusicData music = score({ { 0, { main, tuplet } } });
    CHECK(movePitches(music, { "grace" }, PitchMove::StepUp).changes.size() == 1);
    CHECK(movePitches(music, { "inTuplet" }, PitchMove::StepUp).changes.size() == 1);
}

TEST_CASE("unpitched notes and unknown IDs are left out", "[edit]")
{
    MusicData music = score(
        { { 0, { chord("c", { UnpitchedNote { .id = "drum", .kitEntryId = "k" }, note("n", Step::C, 0, 4) }) } } });
    const Edit edit = movePitches(music, { "drum", "nothing", "n" }, PitchMove::SemitoneUp);
    REQUIRE(edit.changes.size() == 1);
    CHECK(edit.changes[0].noteId == "n");
}

TEST_CASE("undo and redo put the pitches back and forth", "[edit]")
{
    MusicData music = oneNote(Step::C, 0, 4);
    EditHistory history;
    CHECK_FALSE(history.undo(music));

    history.apply(music, movePitches(music, { "n" }, PitchMove::SemitoneUp));
    history.apply(music, movePitches(music, { "n" }, PitchMove::SemitoneUp));
    CHECK(pitchOf(music, "n") == Pitch { Step::D, 0, 4 });
    REQUIRE(history.nextUndo() != nullptr);
    CHECK(history.nextUndo()->name == "Move pitch up");

    CHECK(history.undo(music));
    CHECK(pitchOf(music, "n") == Pitch { Step::C, 1, 4 });
    CHECK(history.undo(music));
    CHECK(pitchOf(music, "n") == Pitch { Step::C, 0, 4 });
    CHECK_FALSE(history.undo(music));

    CHECK(history.redo(music));
    CHECK(pitchOf(music, "n") == Pitch { Step::C, 1, 4 });

    // A new edit drops what could be redone.
    history.apply(music, movePitches(music, { "n" }, PitchMove::OctaveUp));
    CHECK_FALSE(history.redo(music));
    CHECK(pitchOf(music, "n") == Pitch { Step::C, 1, 5 });

    // An edit that changes nothing is no undo step.
    const Edit* before = history.nextUndo();
    history.apply(music, movePitches(music, { "nothing" }, PitchMove::OctaveUp));
    CHECK(history.nextUndo() == before);
}
