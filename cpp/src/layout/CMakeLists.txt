# msd_layout: turns MusicData + a LayoutProfile + SMuFL font metadata into boxes.
# No JUCE: it runs (and is tested) without a window.
add_library(msd_layout STATIC
    Accidentals.cpp
    Accidentals.h
    Geometry.h
    PageLayout.cpp
    PageLayout.h
    SmuflFont.cpp
    SmuflFont.h
    SmuflGlyphNames.cpp
    SmuflGlyphNames.h
    StaffPosition.cpp
    StaffPosition.h
    SystemLayout.cpp
    SystemLayout.h
    Transpose.cpp
    Transpose.h)

target_include_directories(msd_layout PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_features(msd_layout PUBLIC cxx_std_20)
target_link_libraries(msd_layout
    PUBLIC msd_core
    PRIVATE nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(msd_layout PRIVATE /W4 /permissive-)
endif()
