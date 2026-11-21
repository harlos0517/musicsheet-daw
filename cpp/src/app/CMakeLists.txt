juce_add_gui_app(MusicsheetDawApp
    PRODUCT_NAME "Musicsheet DAW"
    COMPANY_NAME "Murata Haru"
    BUNDLE_ID "com.murataharu.musicsheetdaw")

# Files compiled into the app, read through BinaryData:: (Leland_otf, …).
juce_add_binary_data(MsdAppData
    SOURCES
        ${LELAND_DIR}/Leland.otf
        ${LELAND_DIR}/leland_metadata.json
        ${SMUFL_DIR}/metadata/glyphnames.json
        ${PROJECT_SOURCE_DIR}/resources/default.layout.json)

target_sources(MusicsheetDawApp PRIVATE
    Main.cpp
    MainComponent.cpp
    MainComponent.h
    MusicFont.cpp
    MusicFont.h
    ScoreView.cpp
    ScoreView.h)

target_compile_definitions(MusicsheetDawApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:MusicsheetDawApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:MusicsheetDawApp,JUCE_VERSION>"
    MSD_ENABLE_INSPECTOR=$<BOOL:${MSD_ENABLE_INSPECTOR}>
    # Where the Open dialog starts, and the score shown at startup (development only).
    MSD_EXPORTED_SCORES_DIR="${PROJECT_SOURCE_DIR}/../prototype-web/midi-out")

target_link_libraries(MusicsheetDawApp
    PRIVATE
        MsdAppData
        msd_layout
        juce::juce_gui_basics
        juce::juce_graphics
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSD_ENABLE_INSPECTOR)
    target_link_libraries(MusicsheetDawApp PRIVATE melatonin_inspector)
endif()

# Leland is under the SIL OFL, which asks for its license to travel with the font.
add_custom_command(TARGET MusicsheetDawApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${LELAND_DIR}/LICENSE.txt
        $<TARGET_FILE_DIR:MusicsheetDawApp>/Leland-OFL.txt)
