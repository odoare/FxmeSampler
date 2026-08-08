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

project(FxmeSampler VERSION 1.0.0)

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

# ── Kit plugins ────────────────────────────────────────────────────────────
# Build all kits by default. To build a single kit:
#   cmake -B build -DKIT=BlackWidow
#   cmake -B build -DKIT=Century
set(KIT "" CACHE STRING "Build only this kit (leave empty to build all)")

set(_all_kits BlackWidow Century)

if(KIT STREQUAL "")
    foreach(_kit IN LISTS _all_kits)
        add_subdirectory(Kits/${_kit})
    endforeach()
else()
    if(NOT "${KIT}" IN_LIST _all_kits)
        message(FATAL_ERROR "Unknown KIT '${KIT}'. Valid values: ${_all_kits}")
    endif()
    add_subdirectory(Kits/${KIT})
endif()
