/*
  ==============================================================================

    Theme.h

    The plugin's palette, geometry and small drawing helpers, in one place.
    Tune the look here rather than with literals scattered through components.

    Colours follow the juce::Colours idiom (const objects at namespace scope).
    Nothing here is touched during static initialisation, so their construction
    order relative to other translation units does not matter.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace fxsampler::theme
{

//==============================================================================
// Palette

/** The editor backdrop, and the base of the diagonal panel gradient. */
inline const juce::Colour background { juce::Colour::fromFloatRGBA (0.15f, 0.15f, 0.25f, 1.0f) };

/** Flat fills behind tabs and the welcome page. */
inline const juce::Colour panel { juce::Colours::black };

/** Body text on a dark background. */
inline const juce::Colour text { juce::Colours::white };

/** Base of the gradient behind a sample group (lighter than the strips). */
inline const juce::Colour groupPanel { juce::Colours::darkgrey.darker (1.0f) };

//==============================================================================
// Per-control accents. One accent per control: the disc stays dark, the accent
// goes on the arc, outline and pointer (see accentSlider).

inline const juce::Colour presetAccent { juce::Colour::fromFloatRGBA (0.45f, 0.55f, 0.85f, 1.0f) };
inline const juce::Colour sendAccent   { juce::Colours::cyan };    // send level sliders
inline const juce::Colour muteAccent   { juce::Colours::orange };
inline const juce::Colour soloAccent   { juce::Colours::green };
inline const juce::Colour routeAccent  { juce::Colours::white };   // routing toggles
inline const juce::Colour groupAccent  { juce::Colours::purple };  // sample group controls

/** Fallback strip colours, used when mapping.xml gives a strip no colour. */
inline const juce::Colour ambisonicStripDefault { juce::Colours::orange };
inline const juce::Colour stereoStripDefault    { juce::Colours::cyan };
inline const juce::Colour msStripDefault        { juce::Colours::cyan };
inline const juce::Colour monoStripDefault      { juce::Colours::green };
inline const juce::Colour reverbStripDefault    { juce::Colours::purple };
inline const juce::Colour busStripDefault       { juce::Colours::red };

//==============================================================================
// Geometry

/** Welcome tab: fraction of the width given to the preset browser, and the
    bounds it is clamped to so it stays usable without swallowing a narrow
    window. */
inline constexpr float welcomePresetWidthRatio = 0.4f;
inline constexpr int   welcomePresetMinWidth   = 240;
inline constexpr int   welcomePresetMaxWidth   = 460;

/** Welcome tab: fraction of the left-hand column given to the artwork, the
    blurb takes the rest. */
inline constexpr float welcomeImageHeightRatio = 0.8f;

/** Round "i" help button in the editor's top-right corner. */
inline constexpr int infoButtonSize   = 22;
inline constexpr int infoButtonMargin = 4;

//==============================================================================
// Fonts

inline constexpr float welcomeTextHeight = 24.0f;

//==============================================================================
// Helpers

/** Applies one accent colour to a slider: dark disc, accent on the value arc,
    outline and pointer. */
inline void accentSlider (juce::Slider& s, juce::Colour accent)
{
    s.setColour (juce::Slider::trackColourId, accent.darker());
    s.setColour (juce::Slider::thumbColourId, accent);
    s.setColour (juce::Slider::rotarySliderOutlineColourId, accent.darker (2.0f));
}

/** The house backdrop: a gradient running along the component's diagonal, from
    a darkened corner to the base colour. darkenSteps controls how deep the dark
    corner goes (the strips use 2, everything else 3). */
inline void paintPanelBackground (juce::Graphics& g, juce::Rectangle<int> bounds,
                                  juce::Colour base, int darkenSteps = 3)
{
    const auto diagonale = (bounds.getTopLeft() - bounds.getBottomRight()).toFloat();
    const auto length    = diagonale.getDistanceFromOrigin();

    // A component can be painted before it has been given bounds; the gradient
    // maths divides by the diagonal, so fall back to a flat fill.
    if (length <= 0.0f)
    {
        g.fillAll (base);
        return;
    }

    const auto perpendicular = diagonale.rotatedAboutOrigin (juce::degreesToRadians (270.0f)) / length;
    const auto height        = float (bounds.getWidth() * bounds.getHeight()) / length;

    auto dark = base;
    for (int i = 0; i < darkenSteps; ++i)
        dark = dark.darker();

    juce::ColourGradient grad (dark, perpendicular * height,
                               base, perpendicular * -height, false);
    g.setGradientFill (grad);
    g.fillAll();
}

} // namespace fxsampler::theme
