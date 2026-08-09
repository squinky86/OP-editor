# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Jon Hood, OpenPsalm.com

# The format layer: no widgets, no audio, so it can be unit-tested headless
# against the real song corpus.
add_library(ope_core STATIC
    core/Toml.cpp
    core/Notation.cpp
    core/Song.cpp
    core/Lyrics.cpp
    core/Validator.cpp
    core/Library.cpp
    core/Playback.cpp
)
target_include_directories(ope_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ope_core PUBLIC Qt6::Core PRIVATE ope_flags)

# The command-line checker, shared by the app binary and CI.
add_library(ope_cli STATIC cli/Cli.cpp)
target_link_libraries(ope_cli PUBLIC ope_core PRIVATE ope_flags)

add_executable(ope-check cli/CheckMain.cpp)
target_link_libraries(ope-check PRIVATE ope_cli ope_flags)

qt_add_executable(ope
    main.cpp
    app/Session.cpp
    audio/Synth.cpp
    audio/AudioEngine.cpp
    ui/Glyphs.cpp
    ui/ScoreView.cpp
    ui/Panels.cpp
    ui/LyricsPanel.cpp
    ui/Dialogs.cpp
    ui/MainWindow.cpp
)
target_compile_definitions(ope PRIVATE OPE_VERSION="${PROJECT_VERSION}")
target_link_libraries(ope PRIVATE
    ope_core
    ope_cli
    Qt6::Widgets
    Qt6::Gui
    Qt6::Multimedia
    ope_flags
)
set_target_properties(ope PROPERTIES WIN32_EXECUTABLE OFF MACOSX_BUNDLE OFF OUTPUT_NAME ope)

install(TARGETS ope ope-check RUNTIME DESTINATION bin)
