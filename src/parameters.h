#ifndef AUDIO_BRIDGE_PARAMETERS_H_
#define AUDIO_BRIDGE_PARAMETERS_H

#include <algorithm>

#include "pluginterfaces/vst/vsttypes.h"

namespace audio_bridge::vst {

inline constexpr Steinberg::Vst::ParamID kInputGain = 0;
inline constexpr Steinberg::Vst::ParamID kOutputGain = 1;
inline constexpr Steinberg::Vst::ParamID kExclusive = 2;
inline constexpr Steinberg::Vst::ParamID kOutputDelay = 3;
inline constexpr Steinberg::Vst::ParamID kInputDelay = 4;

inline constexpr Steinberg::Vst::ParamID kInputGainRight = 5;
inline constexpr Steinberg::Vst::ParamID kOutputGainRight = 6;
inline constexpr Steinberg::Vst::ParamID kInputLink = 7;
inline constexpr Steinberg::Vst::ParamID kOutputLink = 8;
inline constexpr Steinberg::Vst::ParamID kInputMuteLeft = 9;
inline constexpr Steinberg::Vst::ParamID kInputMuteRight = 10;
inline constexpr Steinberg::Vst::ParamID kOutputMuteLeft = 11;
inline constexpr Steinberg::Vst::ParamID kOutputMuteRight = 12;

inline constexpr double kMinimumGainDb = -20.0;
inline constexpr double kMaximumGainDb = 20.0;
inline constexpr double kMaximumDelayMs = 10000.0;
inline constexpr double kMaximumKnobDelayMs = 1000.0;

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
  return std::clamp(normalized, 0.0, 1.0) * kMaximumDelayMs;
}

inline auto DelayMsToNormalized(const double delay_ms) -> double {
  return std::clamp(delay_ms / kMaximumDelayMs, 0.0, 1.0);
}

}  // namespace audio_bridge::vst

#endif
