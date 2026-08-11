# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Jon Hood, OpenPsalm.com

set(OPE_TESTS FormatTests DocumentTests CorpusTests)

foreach(test IN LISTS OPE_TESTS)
    qt_add_executable(${test} ${test}.cpp Fixtures.cpp)
    target_link_libraries(${test} PRIVATE ope_core Qt6::Test ope_flags)
    target_compile_definitions(${test} PRIVATE
        OPE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# The session layer sits above core/ and holds the operations more than one view
# calls, so it is tested with the document tree rather than through the widgets.
set(OPE_APP_TESTS SessionTests)

foreach(test IN LISTS OPE_APP_TESTS)
    qt_add_executable(${test} ${test}.cpp Fixtures.cpp ${CMAKE_SOURCE_DIR}/src/app/Session.cpp)
    target_link_libraries(${test} PRIVATE ope_core Qt6::Gui Qt6::Test ope_flags)
    target_compile_definitions(${test} PRIVATE
        OPE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# The synth is deterministic and needs no multimedia device. This catches
# transport/resume regressions without making the test suite depend on audio
# hardware or a desktop sound server.
qt_add_executable(AudioTests AudioTests.cpp ${CMAKE_SOURCE_DIR}/src/audio/Synth.cpp)
target_link_libraries(AudioTests PRIVATE ope_core Qt6::Test ope_flags)
add_test(NAME AudioTests COMMAND AudioTests)
set_tests_properties(AudioTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(CliTests CliTests.cpp)
target_link_libraries(CliTests PRIVATE ope_cli Qt6::Test ope_flags)
add_test(NAME CliTests COMMAND CliTests)
set_tests_properties(CliTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Focused widget lifecycle coverage. These tests use real editor panels while
# keeping files in a temporary directory and audio completely out of process.
qt_add_executable(UiWorkflowTests
    UiWorkflowTests.cpp
    Fixtures.cpp
    ${CMAKE_SOURCE_DIR}/src/app/Session.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/LyricsPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/Panels.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/Dialogs.cpp
)
target_link_libraries(UiWorkflowTests PRIVATE ope_core Qt6::Widgets Qt6::Test ope_flags)
target_compile_definitions(UiWorkflowTests PRIVATE OPE_VERSION="test")
add_test(NAME UiWorkflowTests COMMAND UiWorkflowTests)
set_tests_properties(UiWorkflowTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME GuiVersion COMMAND $<TARGET_FILE:ope> --version)
set_tests_properties(GuiVersion PROPERTIES PASS_REGULAR_EXPRESSION "OpenPsalm Editor 0.0.1")

add_test(NAME GuiScreenshot
    COMMAND $<TARGET_FILE:ope> --screenshot ${CMAKE_CURRENT_BINARY_DIR}/ope-smoke.png)
set_tests_properties(GuiScreenshot PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-data"
    TIMEOUT 15)
