# msd_playback: MusicData + a PlaybackSetup → MusicEvents → MIDI, ported from
# prototype-web/src/core/playback. No JUCE: it runs (and is tested) without a window.
add_library(msd_playback STATIC
    BuildCurves.cpp
    BuildCurves.h
    BuildEvents.cpp
    BuildEvents.h
    BuildNotes.cpp
    BuildNotes.h
    Curves.cpp
    Curves.h
    Interpretation.cpp
    Interpretation.h
    JsMath.h
    MidiFile.cpp
    MidiFile.h
    Play.cpp
    Play.h
    Rack.cpp
    Rack.h
    Sequence.cpp
    Sequence.h
    RenderMidi.cpp
    RenderMidi.h
    Repeats.cpp
    Repeats.h
    ScoreIndex.cpp
    ScoreIndex.h
    Swells.cpp
    Swells.h
    Tempo.cpp
    Tempo.h
    Timeline.cpp
    Timeline.h
    TrackContext.h)

target_include_directories(msd_playback PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_features(msd_playback PUBLIC cxx_std_20)
target_link_libraries(msd_playback PUBLIC msd_core)

if(MSVC)
    target_compile_options(msd_playback PRIVATE /W4 /permissive-)
endif()
