# Black Widow Drums plugin
# JUCE and FxmeTools are set up by the root CMakeLists.txt.
# All paths below are relative to this file's directory (Kits/BlackWidow/).

juce_add_plugin(BlackWidowDrums
    COMPANY_NAME                "Fx-Mechanics"
    PLUGIN_MANUFACTURER_CODE    FXME
    PLUGIN_CODE                 BWDR
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "Black Widow Drums"
    DESCRIPTION                 "Gretsch Black Widow Drums"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             "Instrument|Synth"
    BUNDLE_ID                   "com.fxmechanics.BlackWidowDrums"
)

juce_add_binary_data(BlackWidowDrumsBinaryData SOURCES
    # Mapping
    data/mapping.xml

    # Presets
    data/Presets/Canyon.xml
    "data/Presets/Detune+Reverb.xml"
    data/Presets/Factory.xml
    data/Presets/Jungle.xml
    data/Presets/NaturalAcoustic.xml
    data/Presets/Reverbered.xml
    data/Presets/Saturation.xml
    data/Presets/Shorty.xml
    data/Presets/ShortyNDelay.xml
    data/Presets/Tiy.xml
    data/Presets/Wide.xml

    # Images — kit-specific
    data/img/BlackWidow_BlackBackground.png
    data/img/Cymb.jpg
    data/img/drums.jpg
    data/img/HH.jpg
    data/img/Kick.jpg
    data/img/Room.jpg
    data/img/Snare.jpg
    data/img/TomH.jpg
    data/img/white_kick.png
    data/img/white_snare.png
    data/img/white_tomh.png
    data/img/white_toml.png
    data/img/whitedrums.jpg

    # Images — shared from repo root
    ../../img/logo686.png
    ../../img/tube.png
    ../../img/tube_bw.png

    # Tube artwork — taken straight from the FxmeFX submodule so it stays in
    # step with TubeComponent, which looks these two up by name (tube2_png /
    # tube2_bw_png) when the On button is toggled.
    ../../FxmeFX/Source/Tube/img/tube2.png
    ../../FxmeFX/Source/Tube/img/tube2_bw.png

    # Sounds
    "data/wav/3 CYMB CRASH18 0 49 49 49.wav"
    "data/wav/3 HH HHf 1 44 44 44.wav"
    "data/wav/3 HH HHO 1 46 46 46.wav"
    "data/wav/4 CYMB CRASH10 0 49 49 49.wav"
    "data/wav/4 CYMB CRASH16 0 57 57 57.wav"
    "data/wav/4 CYMB StackLong 2 54 54 54.wav"
    "data/wav/6 CYMB Ride2 0 50 50 50.wav"
    "data/wav/6 CYMB RideBell 0 53 53 53.wav"
    "data/wav/7 CYMB Ride1 0 51 51 51.wav"
    "data/wav/7 CYMB StackShort 2 55 55 55.wav"
    "data/wav/9 AmbixSnare AmbixStick 0 37 37 37.wav"
    "data/wav/9 ProxSnare ProxStick 0 37 37 37.wav"
    "data/wav/10 AmbixKick AmbixKick 0 36 36 36.wav"
    "data/wav/10 AmbixSnare AmbixSnareSide 0 39 39 39.wav"
    "data/wav/10 ProxKick ProxKick 0 36 36 36.wav"
    "data/wav/10 ProxSnare ProxSnareSide 0 39 39 39.wav"
    "data/wav/12 HH HHC2 1 43 43 43.wav"
    "data/wav/15 AmbixTomL AmbixTomL 0 45 45 45.wav"
    "data/wav/15 ProxTomL ProxTomL 0 45 45 45.wav"
    "data/wav/16 AmbixTomH AmbixTomH 0 47 47 47.wav"
    "data/wav/16 ProxTomH ProxTomH 0 47 47 47.wav"
    "data/wav/18 HH HHC1 1 42 42 42.wav"
    "data/wav/20 AmbixSnare AmbixSnareMiddle 0 38 38 38.wav"
    "data/wav/20 ProxSnare ProxSnareMiddle 0 38 38 38.wav"
    "data/wav/Council Chamber.wav"
    "data/wav/Forest long.wav"
    "data/wav/Forest short.wav"
    "data/wav/Rectangular room large.wav"
    "data/wav/Rectangular room medium.wav"
    "data/wav/Rectangular room small.wav"
    data/wav/Room\ IR1.wav
    data/wav/Room\ IR2.wav
)

# Embedded WAV arrays are huge; optimizing/LTO-ing byte arrays wastes RAM and
# can OOM the machine. Nothing to optimize here.
set_target_properties(BlackWidowDrumsBinaryData PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION OFF)
target_compile_options(BlackWidowDrumsBinaryData PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O0 -fno-lto>
    $<$<CXX_COMPILER_ID:MSVC>:/Od>)

target_sources(BlackWidowDrums PRIVATE
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

target_include_directories(BlackWidowDrums PRIVATE
    ../../Source
    ../../FxmeFX/Source/Compressor
    ../../FxmeFX/Source/ConvolReverb
    ../../FxmeFX/Source/Equalizer
    ../../FxmeFX/Source/StereoDelay
    ../../FxmeFX/Source/Transient
    ../../FxmeFX/Source/Tube
    ../../FxmeFX/Source/VuMeter
)

juce_generate_juce_header(BlackWidowDrums)

target_compile_definitions(BlackWidowDrums
    PUBLIC
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(BlackWidowDrums
    PRIVATE
        BlackWidowDrumsBinaryData
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
fxmetools_attach(BlackWidowDrums)
