#ifndef AUDIO_BRIDGE_OUTPUT_DELAY_H_
#define AUDIO_BRIDGE_OUTPUT_DELAY_H_

#include <cstddef>
#include <vector>

namespace audio_bridge::audio {

class OutputDelay final {
 public:
  void Prepare(double sample_rate, double maximum_delay_ms);
  void Reset() noexcept;
  void Process(float* left, float* right, std::size_t count,
               double delay_ms) noexcept;

 private:
  double sample_rate_ = 48000.0;
  std::vector<float> left_;
  std::vector<float> right_;
  std::size_t write_position_ = 0U;
};

}  // namespace audio_bridge::audio

#endif
