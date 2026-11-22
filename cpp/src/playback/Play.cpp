#include "playback/Play.h"

#include <utility>

#include "playback/BuildEvents.h"
#include "playback/MidiFile.h"

namespace msd
{

Playback play(const MusicData& music, const PlaybackSetup& setup)
{
    std::vector<MeasureInfo> timeline = measureTimeline(music);
    EventResult result = buildMusicEvents(music, setup);
    std::vector<RenderedTrack> tracks = renderTracks(music, setup, result.events, result.tempo, result.warnings);
    std::vector<std::uint8_t> file =
        writeMidiFile(music.title, music.tpq, result.tempo, result.played, timeline, tracks);
    return { .played = std::move(result.played),
             .timeline = std::move(timeline),
             .events = std::move(result.events),
             .tempo = std::move(result.tempo),
             .tracks = std::move(tracks),
             .warnings = std::move(result.warnings),
             .file = std::move(file) };
}

} // namespace msd
