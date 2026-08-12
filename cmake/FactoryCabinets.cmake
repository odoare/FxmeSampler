# Factory cabinet impulse responses for effectChain="Amp".
#
# The Cab effect resolves IR names through BinaryData directly (it lives in the
# FxmeFX submodule and knows nothing about this project's resource provider),
# so an amp chain can only load cabinets that are compiled into the binary. The
# set that ships with the FxmeFX Cab plugin is about 700 kB — small enough to
# put in every target, so that effectChain="Amp" is useful in a mapping that
# names no IRs of its own.
#
# Alongside the wavs, a cabinets.txt manifest is generated listing their file
# names, one per line, and embedded with them. That is how the running plugin
# tells a cabinet IR from any other embedded wav — a kit's BinaryData is mostly
# drum samples, so "every .wav" is not the answer here. Generating the manifest
# from the same glob that embeds the files means the two cannot disagree.
#
# Usage, from a target's CMakeLists:
#
#     fxme_factory_cabinets(MY_CABINETS)
#     juce_add_binary_data(MyBinaryData SOURCES ... ${MY_CABINETS})

# Captured at include time: inside the function this file's directory is no
# longer what CMAKE_CURRENT_LIST_DIR refers to.
set(_fxme_cab_ir_dir "${CMAKE_CURRENT_LIST_DIR}/../FxmeFX/Source/Cab/IR")

function(fxme_factory_cabinets out_var)
    file(GLOB _irs CONFIGURE_DEPENDS "${_fxme_cab_ir_dir}/*.wav")
    list(SORT _irs)

    set(_manifest "")
    foreach(_ir IN LISTS _irs)
        get_filename_component(_name "${_ir}" NAME)
        string(APPEND _manifest "${_name}\n")
    endforeach()

    # Written per target, into its own binary dir, so two targets never race
    # for the same file.
    set(_manifest_file "${CMAKE_CURRENT_BINARY_DIR}/cabinets.txt")
    file(WRITE "${_manifest_file}" "${_manifest}")

    list(LENGTH _irs _count)
    message(STATUS "  factory cabinets: ${_count} IRs")

    set(${out_var} "${_irs};${_manifest_file}" PARENT_SCOPE)
endfunction()
