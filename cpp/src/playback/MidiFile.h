#pragma once

// MIDI messages → a Standard MIDI File. Ported from
// prototype-web/src/core/playback/midiFile.ts.

#include <cstdint>
#include <string>
#include <vector>

#include "playback/RenderMidi.h"
#include "playback/Repeats.h"
#include "playback/Tempo.h"
#include "playback/Timeline.h"

namespace msd
{

// Format 1: a conductor track with the title, tempo and time signatures, then one track
// per entry of `tracks`, named by its `name`. Message times go back from milliseconds to
// ticks through the tempo map, so the file keeps its bars and tempo when opened in a DAW.
// A measure whose length differs from its time signature (a pickup, say) gets a signature
// of its real length, so the DAW's barlines fall where the score's do. `timeline` is the
// one `played` indexes into.
std::vector<std::uint8_t> writeMidiFile(const std::string& title, int tpq, const TempoMap& tempo,
                                        const std::vector<PlayedMeasure>& played,
                                        const std::vector<MeasureInfo>& timeline,
                                        const std::vector<RenderedTrack>& tracks);

} // namespace msd
