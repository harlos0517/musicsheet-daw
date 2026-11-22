# msd_edit: changes to MusicData, with undo. No JUCE: it runs (and is tested) without a
# window; the app turns clicks and keys into these calls.
add_library(msd_edit STATIC
    EditHistory.cpp
    EditHistory.h
    NoteIndex.cpp
    NoteIndex.h
    PitchEdit.cpp
    PitchEdit.h
    Selection.h)

target_include_directories(msd_edit PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_features(msd_edit PUBLIC cxx_std_20)
# msd_layout for keyAlter: the key signature's alteration of a step.
target_link_libraries(msd_edit PUBLIC msd_core PRIVATE msd_layout)

if(MSVC)
    target_compile_options(msd_edit PRIVATE /W4 /permissive-)
endif()
