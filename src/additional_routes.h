#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace audio_bridge::audio {
// The selected external route owns its gain; otherwise the host route owns it.
inline auto MixAdditionalInput(float host, float additional, float gain) noexcept
    -> float {
  return host + additional * gain;
}
struct RouteResult {
  float peak = 0;
  std::uint64_t silence = 0;
};
inline auto RouteAdditionalBlock(const float* host_left, const float* host_right,
    const float* extra_left, const float* extra_right, float input_gain,
    float output_gain, float* host_out_left, float* host_out_right,
    float* extra_out_left, float* extra_out_right, std::size_t count) noexcept
    -> RouteResult {
  RouteResult result;
  const bool external_input = extra_left || extra_right;
  const bool external_output = extra_out_left || extra_out_right;
  bool left_silent = true, right_silent = true;
  for (std::size_t i = 0; i < count; ++i) {
    const float left = external_input
        ? MixAdditionalInput(host_left ? host_left[i] : 0,
            extra_left ? extra_left[i] : 0, input_gain)
        : (host_left ? host_left[i] : 0) * input_gain;
    const float right = external_input
        ? MixAdditionalInput(host_right ? host_right[i] : 0,
            extra_right ? extra_right[i] : 0, input_gain)
        : (host_right ? host_right[i] : 0) * input_gain;
    const float host_gain = external_output ? 1.0f : output_gain;
    if (host_out_left) host_out_left[i] = left * host_gain;
    if (host_out_right) host_out_right[i] = right * host_gain;
    if (extra_out_left) extra_out_left[i] = left * output_gain;
    if (extra_out_right) extra_out_right[i] = right * output_gain;
    left_silent &= left * host_gain == 0;
    right_silent &= right * host_gain == 0;
    result.peak = std::max(result.peak, std::max(std::abs(left), std::abs(right)));
  }
  if (left_silent) result.silence |= 1;
  if (host_out_right && right_silent) result.silence |= 2;
  return result;
}
}
