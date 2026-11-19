include(FetchContent)

# JUCE 8 is dual-licensed: AGPLv3 or commercial. See cpp/README.md, "Licensing".
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

if(MSD_ENABLE_INSPECTOR)
    # MIT licensed. No release tags upstream, so pin a commit.
    FetchContent_Declare(melatonin_inspector
        GIT_REPOSITORY https://github.com/sudara/melatonin_inspector.git
        GIT_TAG        9c483f865854d83fe9873e1c20e3fd772f695f80
        # juce_add_module requires the folder name to match the module name.
        SOURCE_DIR     ${FETCHCONTENT_BASE_DIR}/melatonin_inspector)
    FetchContent_MakeAvailable(melatonin_inspector)
endif()
