# Copyright (C) 2026 Jon Hood, OpenPsalm.com
# SPDX-License-Identifier: AGPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

set(TEST_NAMES
    PitchTests
    DurationTests
    NoteParserTests
    TomlLoaderTests
    OverlayMergerTests
    ValidatorTests
)

foreach(test_name IN LISTS TEST_NAMES)
    add_executable(${test_name} ${test_name}.cpp)
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}
    )
    target_link_libraries(${test_name} PRIVATE
        OpenPsalmCore
        Qt6::Core
        Qt6::Test
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
