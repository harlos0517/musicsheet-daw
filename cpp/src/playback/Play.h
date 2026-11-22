#pragma once

// The whole playback chain. Ported from play() in
// prototype-web/src/core/playback/index.ts.

#include <cstdint>
#include <string>
#include <vector>

#include "core/types/MusicData.h"
#include "core/types/MusicEvent.h"
#include "core/types/PlaybackSetup.h"
#include "playback/RenderMidi.h"
#include "playback/Repeats.h"
#include "playback/Tempo.h"
#include "playback/Timeline.h"

namespace msd
{

// Everything play() makes from a score and a setup (TS: Playback).
// `timeline` points into the MusicData, which must outlive it.
struct Playback
{
    std::vector<PlayedMeasure> played;
    std::vector<MeasureInfo> timeline;
    std::vector<MusicEvent> events;
    TempoMap tempo;
    std::vector<RenderedTrack> tracks; // one per output
    std::vector<std::string> warnings;
    std::vector<std::uint8_t> file; // the .mid bytes
};

// The whole chain: score + setup → MusicEvents → MIDI messages → a MIDI file.
// Throws std::runtime_error as buildMusicEvents does.
Playback play(const MusicData& music, const PlaybackSetup& setup);

} // namespace msd
