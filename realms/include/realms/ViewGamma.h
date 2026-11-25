#pragma once
// Brightness of the 3D view: Yamagi Quake II's "vid_gamma" (its menu's
// brightness slider, 0.1 .. 2.0), higher is brighter. Every client raises
// the final colour of the world, entities, sky and particles to
// 1 / vid_gamma, as yquake2's GL3 renderer does (pow(colour, 1 / vid_gamma)).
// 1 leaves the view as it is.

#include <algorithm>
#include <cmath>

namespace realms {

// The slider's range; 1 when it is not a number.
inline float clampGamma(float vidGamma) {
    return std::isfinite(vidGamma) ? std::clamp(vidGamma, 0.1f, 2.0f) : 1.0f;
}

// The exponent applied to the final colour.
inline float gammaExponent(float vidGamma) { return 1.0f / clampGamma(vidGamma); }

} // namespace realms
