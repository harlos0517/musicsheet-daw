#pragma once

// MusicEvents → MIDI messages, in milliseconds. Ported from
// prototype-web/src/core/playback/renderMidi.ts, and from the part of play() in
// prototype-web/src/core/playback/index.ts that splits a track over its outputs.

#include <string>
#include <utility>
#include <vector>

#include "core/json/EnumNames.h"
#include "core/types/InstrumentProfile.h"
#include "core/types/MusicData.h"
#include "core/types/MusicEvent.h"
#include "core/types/PlaybackSetup.h"
#include "playback/Tempo.h"

namespace msd
{

enum class MidiMessageType { NoteOn, NoteOff, Cc };
enum class MidiSource { Note, Keyswitch, TechniqueCc, Curve }; // what produced it, for reading the output

struct MidiMessage
{
    double timeMs = 0;
    MidiMessageType type = MidiMessageType::NoteOn;
    int channel = 1; // 1–16
    int data1 = 0;   // note or controller number
    int data2 = 0;   // velocity or controller value
    MidiSource source = MidiSource::Note;
};

struct TrackRender // TS: TrackRender, what renderTrack returns
{
    std::vector<MidiMessage> messages;
    std::vector<std::string> warnings;
};

// One entry of .midi.json (TS: Playback['tracks'][number]): one per output, since a
// sheet track can play on several DAW tracks.
struct RenderedTrack
{
    NodeId trackId;
    std::string outputId;
    std::string name;
    std::vector<MidiMessage> messages;
};

// Same-time order: releases first, then controllers and keyswitches, then new notes.
int messageOrder(const MidiMessage& message);

// One output's MusicEvents → its MIDI messages, sorted by time. The profile's
// trackDelayMs is left out on purpose: it belongs on the DAW track.
TrackRender renderTrack(const std::vector<MusicEvent>& events, const InstrumentProfile& profile, const TempoMap& tempo,
                        const std::vector<TechniqueDefinition>& techniques);

// Every track of the score on each of its outputs, in score order. Curves go to every
// output of a track; kit notes to the output whose `sounds` names them, or, without
// one, to the output that has no `sounds`. Warnings are added to `warnings`.
std::vector<RenderedTrack> renderTracks(const MusicData& music, const PlaybackSetup& setup,
                                        const std::vector<MusicEvent>& events, const TempoMap& tempo,
                                        std::vector<std::string>& warnings);

// Names as in .midi.json. Written out here rather than in core/json/EnumNames.h, which
// must not depend on playback.
template <>
struct EnumNames<MidiMessageType>
{
    static constexpr std::pair<MidiMessageType, std::string_view> entries[] = {
        { MidiMessageType::NoteOn, "note-on" },
        { MidiMessageType::NoteOff, "note-off" },
        { MidiMessageType::Cc, "cc" },
    };
};

template <>
struct EnumNames<MidiSource>
{
    static constexpr std::pair<MidiSource, std::string_view> entries[] = {
        { MidiSource::Note, "note" },
        { MidiSource::Keyswitch, "keyswitch" },
        { MidiSource::TechniqueCc, "technique-cc" },
        { MidiSource::Curve, "curve" },
    };
};

} // namespace msd
