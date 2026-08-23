#ifndef AUDIO_BRIDGE_WASAPI_IO_H_
#define AUDIO_BRIDGE_WASAPI_IO_H_

#include <cstddef>
#include <memory>

#include "audio_backend.h"

namespace audio_bridge::audio {

// Owns the native WASAPI backend. Backend selection and dispatch are handled
// by AudioIo; this class receives raw WASAPI device IDs only.
class WasapiIo final {
 public:
  explicit WasapiIo(StatusCallback callback);
  ~WasapiIo();

  WasapiIo(const WasapiIo&) = delete;
  auto operator=(const WasapiIo&) -> WasapiIo& = delete;

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
