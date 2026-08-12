cmake_minimum_required(VERSION 3.22)

# These MUST be set before project(): that is where CMake configures the Apple
# toolchain and reads them. Set afterwards, the deployment target is ignored
# outright and the architectures are unreliable, which on an Apple Silicon
# runner silently produces an arm64-only "universal" build.
# 10.13 (JUCE 8's floor) rather than 11.0: 11.0 excludes Catalina even with a
# correct x86_64 slice. The arm64 slice is clamped up to 11.0 by the toolchain
# on its own, so a low target costs nothing.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures")
endif()

project(FxmeSampler VERSION 0.3.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is expected as a sibling directory: ../JUCE
add_subdirectory(../JUCE JUCE)

# FxmeTools (shared GUI controls, look-and-feel, DSP, and the WDL convolution
# engine) ships as a submodule of FxmeFX at FxmeFX/lib/FxmeTools. Including its
# .cmake registers the `FxmeTools` JUCE module once and defines the
# fxmetools_attach() helper the kits call. It replaces the old FxmeJuceTools
# module: the two both define fxme::FxmeSlider / FxmeButton / FxmeMeters, so
# they cannot be linked into the same target.
include(${CMAKE_CURRENT_SOURCE_DIR}/FxmeFX/lib/FxmeTools/cmake/FxmeTools.cmake)

# The FxmeFX effect components include Source/Common/TopBar.h, which embeds the
# shared FX-Mechanics logo. This registers the FxmeCommonBinaryData target that
# provides it — once, for both kits (the file has an include_guard, and its
# dedicated BinaryData namespace keeps it from colliding with each kit's own).
include(${CMAKE_CURRENT_SOURCE_DIR}/FxmeFX/Source/Common/CommonAssets.cmake)

# Defines fxme_factory_cabinets(), which every target calls to embed the shared
# cabinet IR set that effectChain="Amp" falls back to.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FactoryCabinets.cmake)

# ── Kit plugins ────────────────────────────────────────────────────────────
# Build the shipping kits by default. To build a single kit:
#   cmake -B build -DKIT=BlackWidow
#   cmake -B build -DKIT=Century
#   cmake -B build -DKIT=BassTest
# KIT=None builds no kit at all, which is what you want alongside
# -DBUILD_DEV_HOST=ON.
set(KIT "" CACHE STRING "Build only this kit (leave empty to build the default set, None for no kit)")

# Every kit that -DKIT= will accept, and the subset built when KIT is empty.
# They differ because BassTest is a test instrument for the loop machinery, not
# a product: the release workflow builds this whole project, so putting it in
# the default set would compile a third plugin into every release for something
# that is never packaged. Move it into _default_kits if that changes.
set(_all_kits     BlackWidow Century BassTest)
set(_default_kits BlackWidow Century)

if(KIT STREQUAL "")
    foreach(_kit IN LISTS _default_kits)
        add_subdirectory(Kits/${_kit})
    endforeach()
elseif(KIT STREQUAL "None")
    message(STATUS "KIT=None: building no kit plugin")
else()
    if(NOT "${KIT}" IN_LIST _all_kits)
        message(FATAL_ERROR "Unknown KIT '${KIT}'. Valid values: ${_all_kits} None")
    endif()
    add_subdirectory(Kits/${KIT})
endif()

# ── Dev host ───────────────────────────────────────────────────────────────
# A standalone application that loads an uncompiled kit folder at runtime, for
# authoring mappings. Off by default: it is a tool, not a product, and the
# release workflow builds this whole project.
#   cmake -B build-dev -DCMAKE_BUILD_TYPE=Release -DKIT=None -DBUILD_DEV_HOST=ON
option(BUILD_DEV_HOST "Build the FxmeSamplerDev standalone kit loader" OFF)

if(BUILD_DEV_HOST)
    add_subdirectory(Dev)
endif()
