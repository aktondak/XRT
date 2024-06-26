# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2019-2022 Xilinx, Inc. All rights reserved.
#
CMAKE_MINIMUM_REQUIRED(VERSION 3.0.0)
PROJECT(XRT_TESTCASES)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_VERBOSE_MAKEFILE ON)
set(XILINX_XRT $ENV{XILINX_XRT})
set(XRT_CORE_LIBRARY xrt_core)

if (MSVC)
  add_compile_options(/Zc:__cplusplus)
endif()

if (NOT DEFINED ${INSTALL_DIR})
  set(INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/${CMAKE_SYSTEM_NAME}/${CMAKE_BUILD_TYPE}/${MODE}")
endif()

add_subdirectory(00_hello)
add_subdirectory(02_simple)
add_subdirectory(03_loopback)
add_subdirectory(04_swizzle)
add_subdirectory(07_sequence)
add_subdirectory(11_fp_mmult256)
add_subdirectory(13_add_one)
add_subdirectory(56_xclbin)
add_subdirectory(abort)
add_subdirectory(fa_kernel)
add_subdirectory(mailbox)
add_subdirectory(query)
add_subdirectory(enqueue)
add_subdirectory(m2m_arg)
if (NOT WIN32)
  add_subdirectory(102_multiproc_verify)
endif(NOT WIN32)
