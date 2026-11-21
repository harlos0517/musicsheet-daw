# Builds THIRD_PARTY_NOTICES.txt: the summary in /THIRD_PARTY_NOTICES.md followed by
# the full text of every license, read from the fetched sources so they stay exact.
# Runs at configure time; the app's build copies the result next to the exe.

set(_notices "${CMAKE_BINARY_DIR}/THIRD_PARTY_NOTICES.txt")
file(READ "${PROJECT_SOURCE_DIR}/../THIRD_PARTY_NOTICES.md" _text)
string(APPEND _text "\n\n# Full license texts\n")

set(_juce_modules "${juce_SOURCE_DIR}/modules")
set(_licenses
    "JUCE|${juce_SOURCE_DIR}/LICENSE.md"
    "JUCE is used under the GNU Affero General Public License v3|${PROJECT_SOURCE_DIR}/../LICENSE"
    "zlib|${_juce_modules}/juce_core/zip/zlib/README"
    "libpng|${_juce_modules}/juce_graphics/image_formats/pnglib/LICENSE"
    "IJG JPEG library|${_juce_modules}/juce_graphics/image_formats/jpglib/README"
    "HarfBuzz|${_juce_modules}/juce_graphics/fonts/harfbuzz/COPYING"
    "SheenBidi|${_juce_modules}/juce_graphics/unicode/sheenbidi/LICENSE"
    "nlohmann/json|${nlohmann_json_SOURCE_DIR}/LICENSE.MIT"
    "Leland|${LELAND_DIR}/LICENSE.txt"
    "SMuFL|${SMUFL_DIR}/markdown/preamble/license.md")
if(MSD_ENABLE_INSPECTOR)
    list(APPEND _licenses "melatonin_inspector (Debug builds)|${melatonin_inspector_SOURCE_DIR}/LICENSE")
endif()

foreach(_entry IN LISTS _licenses)
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _file)
    if(NOT EXISTS "${_file}")
        message(FATAL_ERROR "License file for ${_name} not found: ${_file}")
    endif()
    file(READ "${_file}" _license)
    string(APPEND _text "\n\n## ${_name}\n\n${_license}")
endforeach()

file(WRITE "${_notices}" "${_text}")
set(MSD_NOTICES_FILE "${_notices}" CACHE INTERNAL "Generated third-party notices")

# Re-run configure when the summary or the license text changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/../THIRD_PARTY_NOTICES.md" "${PROJECT_SOURCE_DIR}/../LICENSE")
