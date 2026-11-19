juce_add_gui_app(MusicsheetDawApp
    PRODUCT_NAME "Musicsheet DAW"
    COMPANY_NAME "Murata Haru"
    BUNDLE_ID "com.murataharu.musicsheetdaw")

target_sources(MusicsheetDawApp PRIVATE
    Main.cpp
    MainComponent.cpp
    MainComponent.h)

target_compile_definitions(MusicsheetDawApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:MusicsheetDawApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:MusicsheetDawApp,JUCE_VERSION>"
    MSD_ENABLE_INSPECTOR=$<BOOL:${MSD_ENABLE_INSPECTOR}>)

target_link_libraries(MusicsheetDawApp
    PRIVATE
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
