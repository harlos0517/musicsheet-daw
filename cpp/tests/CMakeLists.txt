add_executable(msd_tests
    EnumNamesTests.cpp
    LayoutTests.cpp
    LoadTests.cpp
    NoteTests.cpp
    PageTests.cpp
    TimingTests.cpp
    CurveTests.cpp
    EventTests.cpp
    MidiTests.cpp
    EditTests.cpp
    RackTests.cpp)

target_link_libraries(msd_tests PRIVATE msd_core msd_layout msd_playback msd_edit nlohmann_json::nlohmann_json Catch2::Catch2WithMain)

target_compile_definitions(msd_tests PRIVATE
    MSD_RESOURCES_DIR="${PROJECT_SOURCE_DIR}/resources"
    MSD_LELAND_DIR="${LELAND_DIR}"
    MSD_SMUFL_DIR="${SMUFL_DIR}"
    MSD_EXPORTED_SCORES_DIR="${PROJECT_SOURCE_DIR}/../prototype-web/midi-out")

if(MSVC)
    target_compile_options(msd_tests PRIVATE /W4 /permissive-)
endif()

# Registers every TEST_CASE with CTest, so `ctest` runs them one by one.
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(msd_tests)
