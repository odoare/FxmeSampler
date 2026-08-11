# FxmeSamplerDev — a standalone application that loads a kit folder at runtime.
#
# Not a plugin, and deliberately so: the kit has to be chosen before the
# processor is constructed, which only an application can do (see Dev/Main.cpp).
# It is off by default; turn it on with -DBUILD_DEV_HOST=ON.
#
# JUCE and FxmeTools are set up by the root CMakeLists.txt.

juce_add_gui_app(FxmeSamplerDev
    COMPANY_NAME                "Fx-Mechanics"
    PRODUCT_NAME                "FxmeSamplerDev"
    BUNDLE_ID                   "com.fxmechanics.FxmeSamplerDev"
    DESCRIPTION                 "Runtime kit loader for FxmeSampler"
)

# The kit being tested supplies its own mapping, samples and artwork from disk,
# so nothing kit-specific is embedded here. What is embedded is the material a
# mapping expects to find in the binary rather than in its folder:
#
#   * the shared artwork under img/, and the tube images TubeComponent looks up
#     by name;
#   * the impulse responses, because ConvolReverb (in the FxmeFX submodule)
#     resolves IR names through BinaryData itself and has no idea a folder
#     provider exists. Without these, a mapping's reverb bus would load silence.
#
# They are listed explicitly rather than globbed: the folder they live in also
# holds ~300 MB of drum samples.
file(GLOB DEV_IMAGES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../img/*.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../img/*.jpg")

set(_ir_dir "${CMAKE_CURRENT_SOURCE_DIR}/../Kits/BlackWidow/data/wav")
set(DEV_IRS
    "${_ir_dir}/Council Chamber.wav"
    "${_ir_dir}/Forest long.wav"
    "${_ir_dir}/Forest short.wav"
    "${_ir_dir}/Rectangular room large.wav"
    "${_ir_dir}/Rectangular room medium.wav"
    "${_ir_dir}/Rectangular room small.wav"
    "${_ir_dir}/Room IR1.wav"
    "${_ir_dir}/Room IR2.wav")

foreach(_ir IN LISTS DEV_IRS)
    if(NOT EXISTS "${_ir}")
        message(WARNING "FxmeSamplerDev: missing impulse response ${_ir}")
    endif()
endforeach()

juce_add_binary_data(FxmeSamplerDevBinaryData SOURCES
    ${DEV_IMAGES}
    ${DEV_IRS}
    ../FxmeFX/Source/Tube/img/tube2.png
    ../FxmeFX/Source/Tube/img/tube2_bw.png
)

# Byte arrays: nothing to optimize, and LTO on them wastes RAM.
set_target_properties(FxmeSamplerDevBinaryData PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION OFF)
target_compile_options(FxmeSamplerDevBinaryData PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O0 -fno-lto>
    $<$<CXX_COMPILER_ID:MSVC>:/Od>)

target_sources(FxmeSamplerDev PRIVATE
    Main.cpp
    # Shared engine — the same files a kit compiles.
    ../Source/EffectChain.cpp
    ../Source/EffectChainComponent.cpp
    ../Source/EffectChainDelay.cpp
    ../Source/EffectChainDelayComponent.cpp
    ../Source/EffectChainDynamics.cpp
    ../Source/EffectChainDynamicsComponent.cpp
    ../Source/EffectChainReverb.cpp
    ../Source/EffectChainReverbComponent.cpp
    ../Source/Mixer.cpp
    ../Source/MixerComponent.cpp
    ../Source/MixerStrips.cpp
    ../Source/MixerStripComponents.cpp
    ../Source/PluginEditor.cpp
    ../Source/PluginProcessor.cpp
    ../Source/ResourceProvider.cpp
    ../Source/Sampler.cpp
    ../Source/SamplerComponent.cpp
    ../Source/SampleGroupComponent.cpp
    ../Source/WelcomeTabComponent.cpp
    # FxmeFX effects submodule
    ../FxmeFX/Source/Compressor/Compressor.cpp
    ../FxmeFX/Source/Compressor/CompressorComponent.cpp
    ../FxmeFX/Source/ConvolReverb/ConvolReverb.cpp
    ../FxmeFX/Source/ConvolReverb/ConvolReverbComponent.cpp
    ../FxmeFX/Source/Equalizer/Equalizer.cpp
    ../FxmeFX/Source/Equalizer/EqualizerComponent.cpp
    ../FxmeFX/Source/StereoDelay/StereoDelay.cpp
    ../FxmeFX/Source/StereoDelay/StereoDelayComponent.cpp
    ../FxmeFX/Source/Transient/Transient.cpp
    ../FxmeFX/Source/Transient/TransientComponent.cpp
    ../FxmeFX/Source/Tube/Tube.cpp
    ../FxmeFX/Source/Tube/TubeComponent.cpp
)

target_include_directories(FxmeSamplerDev PRIVATE
    ../Source
    ../FxmeFX/Source/Compressor
    ../FxmeFX/Source/ConvolReverb
    ../FxmeFX/Source/Equalizer
    ../FxmeFX/Source/StereoDelay
    ../FxmeFX/Source/Transient
    ../FxmeFX/Source/Tube
    ../FxmeFX/Source/VuMeter
)

juce_generate_juce_header(FxmeSamplerDev)

# The shared engine is plugin code and reads the JucePlugin_ macros that
# juce_add_plugin would have defined. An application gets none of them, so the
# handful that are actually referenced are spelled out here. They describe the
# processor, not the host: it is a synth, it wants MIDI in, it sends none.
target_compile_definitions(FxmeSamplerDev
    PUBLIC
        JucePlugin_Name="FxmeSamplerDev"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JucePlugin_IsSynth=1
        JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=0
        JucePlugin_IsMidiEffect=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(FxmeSamplerDev
    PRIVATE
        FxmeSamplerDevBinaryData
        FxmeCommonBinaryData
        juce_audio_basics
        juce_audio_devices
        juce_audio_formats
        juce_audio_processors
        juce_audio_processors_headless
        juce_audio_utils
        juce_core
        juce_data_structures
        juce_events
        juce_graphics
        juce_gui_basics
        juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

fxmetools_attach(FxmeSamplerDev)
