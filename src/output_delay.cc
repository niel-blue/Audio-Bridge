#include "output_delay.h"

#include <algorithm>
#include <cmath>

namespace audio_bridge::audio {

void OutputDelay::Prepare(const double sample_rate,
                          const double maximum_delay_ms) {
  sample_rate_ = std::max(sample_rate, 1.0);
  const auto maximum_frames = static_cast<std::size_t>(std::ceil(
      sample_rate_ * std::max(maximum_delay_ms, 0.0) / 1000.0));
  left_.assign(maximum_frames + 1U, 0.0F);
  right_.assign(maximum_frames + 1U, 0.0F);
  write_position_ = 0U;
}

void OutputDelay::Reset() noexcept {
  std::fill(left_.begin(), left_.end(), 0.0F);
  std::fill(right_.begin(), right_.end(), 0.0F);
  write_position_ = 0U;
}

void OutputDelay::Process(float* const left, float* const right,
                          const std::size_t count,
                          const double delay_ms) noexcept {
  if (left == nullptr || right == nullptr || count == 0U || left_.empty()) {
    return;
  }
  const auto delay_frames = std::clamp(
      std::max(delay_ms, 0.0) * sample_rate_ / 1000.0, 0.0,
      static_cast<double>(left_.size() - 1U));
  const auto whole_frames = static_cast<std::size_t>(delay_frames);
  const auto fraction = delay_frames - static_cast<double>(whole_frames);
  const auto capacity = left_.size();
  for (std::size_t index = 0U; index < count; ++index) {
    const auto input_left = left[index];
    const auto input_right = right[index];
    left_[write_position_] = input_left;
    right_[write_position_] = input_right;

    const auto current =
        (write_position_ + capacity - whole_frames) % capacity;
    const auto previous = (current + capacity - 1U) % capacity;
    left[index] = left_[current] * static_cast<float>(1.0 - fraction) +
                  left_[previous] * static_cast<float>(fraction);
    right[index] = right_[current] * static_cast<float>(1.0 - fraction) +
                   right_[previous] * static_cast<float>(fraction);
    write_position_ = (write_position_ + 1U) % capacity;
  }
}

}  // namespace audio_bridge::audio
