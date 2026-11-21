include(FetchContent)

# JUCE 8 is dual-licensed: AGPLv3 or commercial. See cpp/README.md, "Licensing".
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# MIT. JSON parsing for the core library (the loader keeps it out of public headers).
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_MakeAvailable(nlohmann_json)

if(MSD_BUILD_TESTS)
    # BSL-1.0. Unit tests only, never shipped.
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.16.1
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(Catch2)
endif()

if(MSD_ENABLE_INSPECTOR)
    # MIT licensed. No release tags upstream, so pin a commit.
    FetchContent_Declare(melatonin_inspector
        GIT_REPOSITORY https://github.com/sudara/melatonin_inspector.git
        GIT_TAG        9c483f865854d83fe9873e1c20e3fd772f695f80
        # juce_add_module requires the folder name to match the module name.
        SOURCE_DIR     ${FETCHCONTENT_BASE_DIR}/melatonin_inspector)
    FetchContent_MakeAvailable(melatonin_inspector)
endif()
