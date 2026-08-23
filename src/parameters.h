#ifndef AUDIO_BRIDGE_PARAMETERS_H_
#define AUDIO_BRIDGE_PARAMETERS_H

#include <algorithm>

#include "pluginterfaces/vst/vsttypes.h"

namespace audio_bridge::vst {

inline constexpr Steinberg::Vst::ParamID kInputGain = 0;
inline constexpr Steinberg::Vst::ParamID kOutputGain = 1;
inline constexpr Steinberg::Vst::ParamID kExclusive = 2;
inline constexpr Steinberg::Vst::ParamID kOutputDelay = 3;

inline constexpr double kMinimumGainDb = -30.0;
inline constexpr double kMaximumGainDb = 30.0;
inline constexpr double kMaximumOutputDelayMs = 2000.0;

inline auto GainDbFromNormalized(const double normalized) -> double {
  return kMinimumGainDb + std::clamp(normalized, 0.0, 1.0) *
                              (kMaximumGainDb - kMinimumGainDb);
}

inline auto GainDbToNormalized(const double gain_db) -> double {
  return std::clamp((gain_db - kMinimumGainDb) /
                        (kMaximumGainDb - kMinimumGainDb),
                    0.0, 1.0);
}

inline auto DelayMsFromNormalized(const double normalized) -> double {
  return std::clamp(normalized, 0.0, 1.0) * kMaximumOutputDelayMs;
}

inline auto DelayMsToNormalized(const double delay_ms) -> double {
  return std::clamp(delay_ms / kMaximumOutputDelayMs, 0.0, 1.0);
}

}  // namespace audio_bridge::vst

#endif
