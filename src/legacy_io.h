#ifndef AUDIO_BRIDGE_LEGACY_IO_H_
#define AUDIO_BRIDGE_LEGACY_IO_H_

#include <cstddef>
#include <memory>

#include "audio_backend.h"

namespace audio_bridge::audio {

// MME and DirectSound bridge. Device IDs are backend-qualified strings from
// audio_device_catalog.h, for example mme:input:0 or dsound:output:{GUID}.
class LegacyIo final {
 public:
  explicit LegacyIo(StatusCallback callback);
  ~LegacyIo();

  LegacyIo(const LegacyIo&) = delete;
  auto operator=(const LegacyIo&) -> LegacyIo& = delete;

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
