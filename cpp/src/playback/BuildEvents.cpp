#include "playback/BuildEvents.h"

#include "playback/BuildCurves.h"
#include "playback/BuildNotes.h"
#include "playback/Interpretation.h"
#include "playback/ScoreIndex.h"
#include "playback/Swells.h"
#include "playback/Timeline.h"
#include "playback/TrackContext.h"

namespace msd
{

EventResult buildMusicEvents(const MusicData& music, const PlaybackSetup& setup)
{
    const std::vector<MeasureInfo> timeline = measureTimeline(music);
    std::vector<PlayedMeasure> played = expandRepeats(music, timeline);
    const ScoreIndex index = indexScore(music, timeline);
    TempoMap tempo = buildTempoMap(music, played, index, timeline);
    std::vector<std::string> warnings;
    std::vector<MusicEvent> events;

    for (const Track& track : music.tracks)
    {
        const auto assignment = setup.trackProfiles.find(track.id);
        if (assignment == setup.trackProfiles.end())
        {
            warnings.push_back("track " + track.id + ": no profiles assigned, skipped");
            continue;
        }
        const InterpretationProfile profile =
            resolveInterpretationProfile(setup.interpretationProfiles, assignment->second.interpretationProfileId);
        const TrackContext ctx { music, setup, timeline, played, index, track, profile, tempo, warnings };
        std::vector<CurveEvent> curves = buildCurves(ctx);
        BuiltNotes notes = buildNotes(ctx, curves);
        std::vector<CurveEvent> swells = buildSwells(ctx, notes.notes, curves, notes.phrases);
        events.reserve(events.size() + curves.size() + swells.size() + notes.notes.size());
        for (CurveEvent& curve : curves)
            events.emplace_back(std::move(curve));
        for (CurveEvent& swell : swells)
            events.emplace_back(std::move(swell));
        for (NoteEvent& note : notes.notes)
            events.emplace_back(std::move(note));
    }
    return { std::move(played), std::move(tempo), std::move(events), std::move(warnings) };
}

} // namespace msd
