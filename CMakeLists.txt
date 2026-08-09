# Copyright (C) 2026 Jon Hood, OpenPsalm.com
# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)
project(OpenPsalmEditor
    VERSION 0.1.0
    DESCRIPTION "Desktop SATB score and TOML editor for OpenPsalm song corpus"
    HOMEPAGE_URL "https://openpsalm.com"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Test
)

# Vendored toml++ library
add_library(tomlplusplus INTERFACE)
target_include_directories(tomlplusplus INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlplusplus
)

# Core and UI libraries & executable
add_subdirectory(src)

# Tests
enable_testing()
add_subdirectory(tests)
