/*
  ==============================================================================

    AmbixToMS.h

    The decoder now lives in FxmeTools (fxme::AmbixToStereo), where the other
    ambisonic projects can use it. This header is a thin re-export kept so the
    existing includes and the AmbixToMS type name stay valid.

  ==============================================================================
*/

#pragma once

#include <FxmeTools/dsp/AmbixToStereo.h>

using AmbixToMS = fxme::AmbixToStereo;
