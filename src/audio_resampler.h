#ifndef AUDIO_BRIDGE_AUDIO_RESAMPLER_H
#define AUDIO_BRIDGE_AUDIO_RESAMPLER_H

#include <cstddef>

#include "audio_fifo.h"

namespace audio_bridge::audio {

// Small linear interpolator used at the boundary between the host clock and
// a legacy device clock. The FIFO is single-producer/single-consumer, so this
// class is only used by the corresponding backend worker thread.
class LinearReader final {
 public:
  LinearReader(StereoFifo& fifo, const double source_rate,
               const double destination_rate)
      : fifo_(fifo),
        ratio_(source_rate > 0.0 && destination_rate > 0.0
                   ? source_rate / destination_rate
                   : 1.0) {}

  void Next(float& left, float& right) noexcept {
    if (!initialized_) {
      static_cast<void>(fifo_.Pop(&previous_left_, &previous_right_, 1U));
      static_cast<void>(fifo_.Pop(&next_left_, &next_right_, 1U));
      initialized_ = true;
    }
    left = previous_left_ + static_cast<float>(phase_) *
                               (next_left_ - previous_left_);
    right = previous_right_ + static_cast<float>(phase_) *
                                  (next_right_ - previous_right_);
    phase_ += ratio_;
    while (phase_ >= 1.0) {
      previous_left_ = next_left_;
      previous_right_ = next_right_;
      static_cast<void>(fifo_.Pop(&next_left_, &next_right_, 1U));
      phase_ -= 1.0;
    }
  }

 private:
  StereoFifo& fifo_;
  double ratio_ = 1.0;
  double phase_ = 0.0;
  float previous_left_ = 0.0F;
  float previous_right_ = 0.0F;
  float next_left_ = 0.0F;
  float next_right_ = 0.0F;
  bool initialized_ = false;
};

}  // namespace audio_bridge::audio

#endif
