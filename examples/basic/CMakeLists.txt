# SPDX-FileCopyrightText: ElaadNL
#
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.16)
get_filename_component(S2_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(S2_COMPONENT_NAME "${S2_ROOT_DIR}" NAME)
set(S2_COMPONENT_NAME "${S2_COMPONENT_NAME}" CACHE STRING "S2 component name (directory-derived)")
set(EXTRA_COMPONENT_DIRS "${S2_ROOT_DIR}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(basic_example)
