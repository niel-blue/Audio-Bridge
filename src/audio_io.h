#ifndef AUDIO_BRIDGE_AUDIO_IO_H_
#define AUDIO_BRIDGE_AUDIO_IO_H_

#include <cstddef>
#include <memory>

#include "audio_backend.h"

namespace audio_bridge::audio {

// Dispatches each selected direction to its device backend. Input and output
// may use different backends at the same time.
class AudioIo final {
 public:
  explicit AudioIo(StatusCallback callback);
  ~AudioIo();

  AudioIo(const AudioIo&) = delete;
  auto operator=(const AudioIo&) -> AudioIo& = delete;

  void Start(const Config& config);
  void Stop() noexcept;

  void PushOutputBlock(const float* left, const float* right,
                       std::size_t count) noexcept;
  void PopInputBlock(float* left, float* right,
                     std::size_t count) noexcept;

  [[nodiscard]] auto InputEnabled() const noexcept -> bool;
  [[nodiscard]] auto OutputEnabled() const noexcept -> bool;
  [[nodiscard]] auto GetStatus() const noexcept -> Status;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace audio_bridge::audio

#endif
