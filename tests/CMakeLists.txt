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
