# msd_core: score types and JSON loading. No JUCE, so tests and layout can use it
# without a window.
add_library(msd_core STATIC
    json/EnumNames.h
    json/Load.cpp
    json/Load.h
    types/LayoutProfile.h
    types/LayoutSetup.h
    types/MusicData.h)

# Includes are written from src/, e.g. #include "core/types/MusicData.h".
target_include_directories(msd_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_features(msd_core PUBLIC cxx_std_20)
target_link_libraries(msd_core PRIVATE nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(msd_core PRIVATE /W4 /permissive-)
endif()
