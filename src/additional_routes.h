#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace audio_bridge::audio {
// An external input replaces the host input when selected.
// Input gain applies only to that source.
struct RouteResult {
  float peak = 0;
  std::uint64_t silence = 0;
};
inline auto RouteSelectedInputBlock(const float* host_left,
    const float* host_right, const float* external_left,
    const float* external_right, float input_gain, float input_gain_right, float output_gain, float output_gain_right,
    float* host_out_left, float* host_out_right,
    float* external_out_left, float* external_out_right,
    std::size_t count) noexcept
    -> RouteResult {
  RouteResult result;
  const bool external_input = external_left || external_right;
  const bool external_output = external_out_left || external_out_right;
  bool left_silent = true, right_silent = true;
  for (std::size_t i = 0; i < count; ++i) {
    const float left = external_input
        ? (external_left ? external_left[i] : 0.0F) * input_gain
        : (host_left ? host_left[i] : 0.0F) * input_gain;
    const float right = external_input
        ? (external_right ? external_right[i] : 0.0F) * input_gain_right
        : (host_right ? host_right[i] : 0.0F) * input_gain_right;
    const float host_gain = external_output ? 1.0f : output_gain;
    const float host_gain_right = external_output ? 1.0f : output_gain_right;
    if (host_out_left) host_out_left[i] = left * host_gain;
    if (host_out_right) host_out_right[i] = right * host_gain_right;
    if (external_out_left) external_out_left[i] = left * output_gain;
    if (external_out_right) external_out_right[i] = right * output_gain_right;
    left_silent &= left * host_gain == 0;
    right_silent &= right * host_gain_right == 0;
    result.peak =
        std::max(result.peak, std::max(std::abs(left), std::abs(right)));
  }
  if (left_silent) result.silence |= 1;
  if (host_out_right && right_silent) result.silence |= 2;
  return result;
}
inline auto RouteSelectedInputBlock(const float* hl, const float* hr,
    const float* el, const float* er, float ig, float og, float* ol,
    float* oright, float* eol, float* eor, std::size_t n) noexcept -> RouteResult {
  return RouteSelectedInputBlock(hl, hr, el, er, ig, ig, og, og, ol, oright, eol, eor, n);
}

}
