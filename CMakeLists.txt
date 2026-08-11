# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Jon Hood, OpenPsalm.com

cmake_minimum_required(VERSION 3.25)
project(OpenPsalmEditor
    VERSION 0.0.1
    DESCRIPTION "Editor for OpenPsalm song.toml files"
    LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(OPE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(OPE_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Multimedia)
find_package(Qt6 6.5 QUIET COMPONENTS Test)

qt_standard_project_setup()

add_library(ope_flags INTERFACE)
# QT_NO_KEYWORDS: Qt's unprefixed `emit`, `slots`, and `signals` macros collide
# with perfectly ordinary identifiers in the format layer. Use Q_EMIT / Q_SLOTS.
target_compile_definitions(ope_flags INTERFACE QT_NO_KEYWORDS)
target_compile_options(ope_flags INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wno-unused-parameter>
    $<$<AND:$<BOOL:${OPE_WARNINGS_AS_ERRORS}>,$<CXX_COMPILER_ID:GNU,Clang>>:-Werror>)
if(OPE_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ope_flags INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ope_flags INTERFACE -fsanitize=address,undefined)
endif()

add_subdirectory(src)

if(Qt6Test_FOUND)
    enable_testing()
    add_subdirectory(tests)
endif()

install(FILES README.md LICENSE CHANGELOG.md OPE.md RELEASE_CHECKLIST.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR})

set(CPACK_PACKAGE_NAME "OpenPsalmEditor")
set(CPACK_PACKAGE_VENDOR "OpenPsalm.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Editor for OpenPsalm song.toml files")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_GENERATOR "TGZ;ZIP")
include(CPack)
