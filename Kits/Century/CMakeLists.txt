# CenturyDrums plugin
# JUCE and FxmeTools are set up by the root CMakeLists.txt.
# All paths below are relative to this file's directory (Kits/Century/).

juce_add_plugin(CenturyDrums
    COMPANY_NAME                "Fx-Mechanics"
    PLUGIN_MANUFACTURER_CODE    FXME
    PLUGIN_CODE                 CTDR
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "CenturyDrums"
    DESCRIPTION                 "CenturyDrums"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             "Instrument|Synth"
    BUNDLE_ID                   "com.fxmechanics.CenturyDrums"
)

# Everything under data/ is embedded; the lists below are globbed rather than
# written out by hand, which had already drifted (a preset that never shipped,
# and several unused takes). CONFIGURE_DEPENDS makes the build re-scan these
# directories and reconfigure when their contents change, so a file dropped in
# data/ is picked up without editing this file.
#
# The flip side is that ANYTHING in data/ ships. Working sources (GIMP
# projects, photo originals, unused takes) belong in art/, which is never
# scanned. The counts below are printed at configure time as a tripwire: if
# they jump unexpectedly, something landed in data/ that should not have.
file(GLOB CT_WAVS    CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Data/wav/*.wav")
file(GLOB CT_PRESETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Data/presets/*.xml")
file(GLOB CT_IMAGES  CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Data/img/*.png"
                                      "${CMAKE_CURRENT_SOURCE_DIR}/Data/img/*.jpg")

list(LENGTH CT_WAVS    _ct_nwav)
list(LENGTH CT_PRESETS _ct_npreset)
list(LENGTH CT_IMAGES  _ct_nimg)
message(STATUS "Century: embedding ${_ct_nwav} samples, ${_ct_npreset} presets, ${_ct_nimg} images")

# Shared cabinet IRs plus their manifest, for effectChain="Amp".
fxme_factory_cabinets(CT_CABINETS)

juce_add_binary_data(CenturyDrumsBinaryData SOURCES
    # Mapping: the one file that must exist, so it stays explicit.
    Data/mapping.xml

    ${CT_PRESETS}
    ${CT_IMAGES}
    ${CT_WAVS}
    ${CT_CABINETS}

    # Shared across kits, from outside this directory.
    ../../img/logo686.png
    ../../img/tube.png
    ../../img/tube_bw.png

    # Tube artwork — taken straight from the FxmeFX submodule so it stays in
    # step with TubeComponent, which looks these two up by name (tube2_png /
    # tube2_bw_png) when the On button is toggled.
    ../../FxmeFX/Source/Tube/img/tube2.png
    ../../FxmeFX/Source/Tube/img/tube2_bw.png
)

# Embedded WAV arrays are huge; optimizing/LTO-ing byte arrays wastes RAM and
# can OOM the machine. Nothing to optimize here.
set_target_properties(CenturyDrumsBinaryData PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION OFF)
target_compile_options(CenturyDrumsBinaryData PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O0 -fno-lto>
    $<$<CXX_COMPILER_ID:MSVC>:/Od>)

target_sources(CenturyDrums PRIVATE
    # Shared engine — relative to this kit directory
    ../../Source/EffectChainAmp.cpp
    ../../Source/EffectChainAmpComponent.cpp
    ../../Source/EffectChain.cpp
    ../../Source/EffectChainComponent.cpp
    ../../Source/EffectChainDelay.cpp
    ../../Source/EffectChainDelayComponent.cpp
    ../../Source/EffectChainDynamics.cpp
    ../../Source/EffectChainDynamicsComponent.cpp
    ../../Source/EffectChainReverb.cpp
    ../../Source/EffectChainReverbComponent.cpp
    ../../Source/Mixer.cpp
    ../../Source/MixerComponent.cpp
    ../../Source/MixerStrips.cpp
    ../../Source/MixerStripComponents.cpp
    ../../Source/PluginEditor.cpp
    ../../Source/PluginProcessor.cpp
    ../../Source/ResourceProvider.cpp
    ../../Source/Sampler.cpp
    ../../Source/SamplerComponent.cpp
    ../../Source/SampleGroupComponent.cpp
    ../../Source/WelcomeTabComponent.cpp
    # FxmeFX effects submodule
    ../../FxmeFX/Source/Compressor/Compressor.cpp
    ../../FxmeFX/Source/Compressor/CompressorComponent.cpp
    ../../FxmeFX/Source/Cab/Cab.cpp
    ../../FxmeFX/Source/Cab/CabComponent.cpp
    ../../FxmeFX/Source/ConvolReverb/ConvolReverb.cpp
    ../../FxmeFX/Source/ConvolReverb/ConvolReverbComponent.cpp
    ../../FxmeFX/Source/Equalizer/Equalizer.cpp
    ../../FxmeFX/Source/Equalizer/EqualizerComponent.cpp
    ../../FxmeFX/Source/StereoDelay/StereoDelay.cpp
    ../../FxmeFX/Source/StereoDelay/StereoDelayComponent.cpp
    ../../FxmeFX/Source/Transient/Transient.cpp
    ../../FxmeFX/Source/Transient/TransientComponent.cpp
    ../../FxmeFX/Source/Tube/Tube.cpp
    ../../FxmeFX/Source/Tube/TubeComponent.cpp
    # The WDL convolution engine (convoengine.cpp/fft.c/resample.cpp) is
    # compiled in by fxmetools_attach() below, from FxmeTools' WDL submodule.
)

target_include_directories(CenturyDrums PRIVATE
    ../../Source
    ../../FxmeFX/Source/Compressor
    ../../FxmeFX/Source/Cab
    ../../FxmeFX/Source/ConvolReverb
    ../../FxmeFX/Source/Equalizer
    ../../FxmeFX/Source/StereoDelay
    ../../FxmeFX/Source/Transient
    ../../FxmeFX/Source/Tube
    ../../FxmeFX/Source/VuMeter
)

juce_generate_juce_header(CenturyDrums)

target_compile_definitions(CenturyDrums
    PUBLIC
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(CenturyDrums
    PRIVATE
        CenturyDrumsBinaryData
        FxmeCommonBinaryData
        juce_audio_basics
        juce_audio_devices
        juce_audio_formats
        juce_audio_plugin_client
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

# Links the FxmeTools module (fxme::VuMeter, fxme::SpectrumAnalyzer,
# fxme::EmbeddedAudio, fxme::FxmeSlider, fxme::FxmeLookAndFeel…) and compiles
# the WDL convolution engine reached through <convoengine.h>.
fxmetools_attach(CenturyDrums)
