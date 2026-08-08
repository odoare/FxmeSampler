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

juce_add_binary_data(CenturyDrumsBinaryData SOURCES
    # Mapping
    Data/mapping.xml

    # Presets
    Data/presets/Blurr.xml
    Data/presets/Detuned.xml
    Data/presets/DetunedNProcessed.xml
    Data/presets/Distorded.xml
    Data/presets/Eighties.xml
    Data/presets/Heavy.xml
    Data/presets/Range.xml
    Data/presets/ShortNDry.xml
    Data/presets/TinyNDelay.xml

    # Images — kit-specific
    Data/img/Cymb.jpg
    Data/img/drums.jpg
    Data/img/HH.jpg
    Data/img/Kick.jpg
    Data/img/Room.jpg
    Data/img/Snare.jpg
    Data/img/TomH.jpg
    Data/img/white_kick.png
    Data/img/white_snare.png
    Data/img/white_tomh.png
    Data/img/white_toml.png
    Data/img/whitedrums.jpg

    # Images — shared from repo root
    ../../img/logo686.png
    ../../img/tube.png
    ../../img/tube_bw.png

    # Tube artwork — taken straight from the FxmeFX submodule so it stays in
    # step with TubeComponent, which looks these two up by name (tube2_png /
    # tube2_bw_png) when the On button is toggled.
    ../../FxmeFX/Source/Tube/img/tube2.png
    ../../FxmeFX/Source/Tube/img/tube2_bw.png

    # Sounds — cymbals
    "Data/wav/CYMB CRASH 01 0 49 49 49.wav"
    "Data/wav/CYMB CRASH 02 0 49 49 49.wav"
    "Data/wav/CYMB CRASH 03 0 49 49 49.wav"
    "Data/wav/CYMB CRASH 04 0 49 49 49.wav"
    "Data/wav/CYMB RIDE1 01 0 51 51 51.wav"
    "Data/wav/CYMB RIDE1 02 0 51 51 51.wav"
    "Data/wav/CYMB RIDE1 03 0 51 51 51.wav"
    "Data/wav/CYMB RIDE1 04 0 51 51 51.wav"
    "Data/wav/CYMB RIDE1 05 0 51 51 51.wav"
    "Data/wav/CYMB RIDE1 06 0 51 51 51.wav"
    "Data/wav/CYMB RIDE1 07 0 51 51 51.wav"
    "Data/wav/CYMB RIDE1 08 0 51 51 51.wav"
    "Data/wav/CYMB RIDE1 09 0 51 51 51.wav"
    "Data/wav/CYMB RIDE1 10 0 51 51 51.wav"
    "Data/wav/CYMB RIDE1 11 0 51 51 51.wav"
    "Data/wav/CYMB RIDE2 01 0 53 53 53.wav"
    "Data/wav/CYMB RIDE2 02 0 53 53 53.wav"
    "Data/wav/CYMB RIDE2 03 0 53 53 53.wav"
    "Data/wav/CYMB RIDE2 05 0 53 53 53.wav"
    "Data/wav/CYMB RIDE2 06 0 53 53 53.wav"
    "Data/wav/CYMB RIDE2 07 0 53 53 53.wav"
    "Data/wav/CYMB RIDE2 08 0 53 53 53.wav"
    "Data/wav/CYMB SPLASH 01 0 55 55 55.wav"
    "Data/wav/CYMB SPLASH 02 0 55 55 55.wav"
    "Data/wav/CYMB SPLASH 03 0 55 55 55.wav"
    "Data/wav/CYMB SPLASH 04 0 55 55 55.wav"

    # Sounds — IRs / rooms
    "Data/wav/Church.wav"
    "Data/wav/Council Chamber.wav"
    "Data/wav/Forest long.wav"
    "Data/wav/Forest short.wav"
    "Data/wav/Rectangular room large.wav"
    "Data/wav/Rectangular room medium.wav"
    "Data/wav/Rectangular room small.wav"

    # Sounds — hi-hats
    "Data/wav/HH CHH 01 1 42 42 42.wav"
    "Data/wav/HH CHH 02 1 42 42 42.wav"
    "Data/wav/HH CHH 03 1 42 42 42.wav"
    "Data/wav/HH CHH 04 1 42 42 42.wav"
    "Data/wav/HH CHH 05 1 42 42 42.wav"
    "Data/wav/HH CHH 06 1 42 42 42.wav"
    "Data/wav/HH CHH 07 1 42 42 42.wav"
    "Data/wav/HH CHH 08 1 42 42 42.wav"
    "Data/wav/HH CHH 09 1 42 42 42.wav"
    "Data/wav/HH CHH 10 1 42 42 42.wav"
    "Data/wav/HH FHH 01 04 44 44 44.wav"
    "Data/wav/HH FHH 01 1 44 44 44.wav"
    "Data/wav/HH FHH 02 1 44 44 44.wav"
    "Data/wav/HH FHH 03 1 44 44 44.wav"
    "Data/wav/HH OHH 01 1 46 46 46.wav"
    "Data/wav/HH OHH 02 1 46 46 46.wav"
    "Data/wav/HH OHH 03 1 46 46 46.wav"
    "Data/wav/HH OHH 04 1 46 46 46.wav"

    # Sounds — kick
    "Data/wav/KICK KICK 01 1 36 36 36.wav"
    "Data/wav/KICK KICK 02 1 36 36 36.wav"
    "Data/wav/KICK KICK 03 1 36 36 36.wav"
    "Data/wav/KICK KICK 04 1 36 36 36.wav"
    "Data/wav/KICK KICK 05 1 36 36 36.wav"
    "Data/wav/KICK KICK 06 1 36 36 36.wav"
    "Data/wav/KICK KICK 07 1 36 36 36.wav"
    "Data/wav/KICK KICK 08 1 36 36 36.wav"
    "Data/wav/KICK KICK 09 1 36 36 36.wav"

    # Sounds — snare
    "Data/wav/SNARE SNARE 01 0 38 38 38.wav"
    "Data/wav/SNARE SNARE 02 0 38 38 38.wav"
    "Data/wav/SNARE SNARE 03 0 38 38 38.wav"
    "Data/wav/SNARE SNARE 04 0 38 38 38.wav"
    "Data/wav/SNARE SNARE 05 0 38 38 38.wav"
    "Data/wav/SNARE SNARE 06 0 38 38 38.wav"
    "Data/wav/SNARE SNARE 07 0 38 38 38.wav"
    "Data/wav/SNARE SNARE 08 0 38 38 38.wav"
    "Data/wav/SNARE SNARE 09 0 38 38 38.wav"
    "Data/wav/SNARE SNARE 10 0 38 38 38.wav"

    # Sounds — toms low
    "Data/wav/TOMB TOMB 01 0 45 45 45.wav"
    "Data/wav/TOMB TOMB 02 0 45 45 45.wav"
    "Data/wav/TOMB TOMB 03 0 45 45 45.wav"
    "Data/wav/TOMB TOMB 04 0 45 45 45.wav"
    "Data/wav/TOMB TOMB 05 0 45 45 45.wav"
    "Data/wav/TOMB TOMB 06 0 45 45 45.wav"
    "Data/wav/TOMB TOMB 07 0 45 45 45.wav"
    "Data/wav/TOMB TOMB 08 0 45 45 45.wav"
    "Data/wav/TOMB TOMB 09 0 45 45 45.wav"

    # Sounds — toms high
    "Data/wav/TOMH TOMH 01 0 47 47 47.wav"
    "Data/wav/TOMH TOMH 02 0 47 47 47.wav"
    "Data/wav/TOMH TOMH 03 0 47 47 47.wav"
    "Data/wav/TOMH TOMH 04 0 47 47 47.wav"
    "Data/wav/TOMH TOMH 05 0 47 47 47.wav"
    "Data/wav/TOMH TOMH 06 0 47 47 47.wav"
    "Data/wav/TOMH TOMH 07 0 47 47 47.wav"
    "Data/wav/TOMH TOMH 08 0 47 47 47.wav"
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
    ../../Source/Sampler.cpp
    ../../Source/SamplerComponent.cpp
    ../../Source/SampleGroupComponent.cpp
    ../../Source/WelcomeTabComponent.cpp
    # FxmeFX effects submodule
    ../../FxmeFX/Source/Compressor/Compressor.cpp
    ../../FxmeFX/Source/Compressor/CompressorComponent.cpp
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
