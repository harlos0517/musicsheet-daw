# Third-party notices

Musicsheet DAW is licensed under the GNU Affero General Public License, version 3
or (at your option) any later version; see `LICENSE`. The app (`cpp/`) also
contains the software, font and data below, each under its own license.

The build appends the full text of each license to this file and copies the
result next to the app as `THIRD_PARTY_NOTICES.txt`.

## In the app

| Component | Version | Copyright | License |
|---|---|---|---|
| JUCE (modules juce_core, juce_events, juce_data_structures, juce_graphics, juce_gui_basics) | 8.0.15 | Raw Material Software Limited | AGPLv3 (JUCE is dual-licensed; used here under the AGPLv3) |
| zlib, inside JUCE | 1.3.1 | Jean-loup Gailly and Mark Adler | zlib License |
| libpng, inside JUCE | 1.6.37 | The PNG Reference Library Authors, Cosmin Truta, Glenn Randers-Pehrson, Andreas Dilger, Guy Eric Schalnat | libpng License |
| IJG JPEG library, inside JUCE | 9f | Thomas G. Lane, Guido Vollbeding | IJG License |
| HarfBuzz, inside JUCE | 10.1.0 | Google, Inc., Ebrahim Byagowi, Facebook, Inc., Mozilla Foundation and others | "Old MIT" License |
| SheenBidi, inside JUCE | as bundled with JUCE 8.0.15 | Muhammad Tayyab Akram | Apache License 2.0 |
| nlohmann/json | 3.12.0 | Niels Lohmann | MIT License |
| Leland music font | 0.80 | MuseScore BVBA, Reserved Font Name "Leland" | SIL Open Font License 1.1 |
| SMuFL `glyphnames.json` | 1.4 | the Contributors to the Standard Music Font Layout Specification | W3C Community Final Specification Agreement |

This software is based in part on the work of the Independent JPEG Group.

The SMuFL notice, as published with the specification: Copyright © 2021 the
Contributors to the Standard Music Font Layout Specification, published by the W3C
Music Notation Community Group under the W3C Community Final Specification
Agreement (FSA), https://www.w3.org/community/about/agreements/final/. A
human-readable summary is at https://www.w3.org/community/about/agreements/fsa-deed/.

## In Debug builds only

| Component | Copyright | License |
|---|---|---|
| melatonin_inspector | Sudara Williams | MIT License |

## Used to build or test, not shipped

Catch2 (Boost Software License 1.0), CMake, Ninja and the MSVC toolchain.
