# Copyright (C) 2026 Jon Hood, OpenPsalm.com
# SPDX-License-Identifier: AGPL-3.0-or-later

set(SRC_CORE
    core/Common.hpp
    core/Common.cpp
    core/PartData.hpp
    core/PartData.cpp
    core/LyricMap.hpp
    core/LyricMap.cpp
    core/SongData.hpp
    core/SongData.cpp
    core/SongDocument.hpp
    core/SongDocument.cpp
    core/Commands.hpp
    core/Commands.cpp
)

set(SRC_NOTATION
    notation/Pitch.hpp
    notation/Pitch.cpp
    notation/Duration.hpp
    notation/Duration.cpp
    notation/NoteToken.hpp
    notation/NoteToken.cpp
    notation/NoteParser.hpp
    notation/NoteParser.cpp
    notation/NoteSerializer.hpp
    notation/NoteSerializer.cpp
    notation/LyricAligner.hpp
    notation/LyricAligner.cpp
)

set(SRC_FORMAT
    format/TomlLoader.hpp
    format/TomlLoader.cpp
    format/OverlayMerger.hpp
    format/OverlayMerger.cpp
    format/TomlSerializer.hpp
    format/TomlSerializer.cpp
)

set(SRC_VALIDATION
    validation/Validator.hpp
    validation/Validator.cpp
)

set(SRC_UI
    ui/DiagnosticsWidget.hpp
    ui/DiagnosticsWidget.cpp
    ui/MetadataEditor.hpp
    ui/MetadataEditor.cpp
    ui/LyricsEditor.hpp
    ui/LyricsEditor.cpp
    ui/NotesEditor.hpp
    ui/NotesEditor.cpp
    ui/InspectorWidget.hpp
    ui/InspectorWidget.cpp
    ui/ScoreView.hpp
    ui/ScoreView.cpp
    ui/SongBrowser.hpp
    ui/SongBrowser.cpp
    ui/MainWindow.hpp
    ui/MainWindow.cpp
)

set(SRC_APP
    app/Settings.hpp
    app/Settings.cpp
    app/Main.cpp
)

# Core library (for tests and application)
add_library(OpenPsalmCore STATIC
    ${SRC_CORE}
    ${SRC_NOTATION}
    ${SRC_FORMAT}
    ${SRC_VALIDATION}
)

target_include_directories(OpenPsalmCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(OpenPsalmCore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Executable
add_executable(OpenPsalmEditor
    ${SRC_UI}
    ${SRC_APP}
)

target_include_directories(OpenPsalmEditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(OpenPsalmEditor PRIVATE
    OpenPsalmCore
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
