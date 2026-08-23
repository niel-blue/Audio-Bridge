#ifndef AUDIO_BRIDGE_AUDIO_FIFO_H_
#define AUDIO_BRIDGE_AUDIO_FIFO_H_

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

namespace audio_bridge::audio {

// Single-producer/single-consumer stereo FIFO. The WASAPI worker owns one side
// and the VST process callback owns the other, so no mutex is used in the
// realtime paths.
class StereoFifo final {
 public:
  explicit StereoFifo(const std::size_t capacity)
      : left_(capacity, 0.0F), right_(capacity, 0.0F), capacity_(capacity) {}

  StereoFifo(const StereoFifo&) = delete;
  auto operator=(const StereoFifo&) -> StereoFifo& = delete;

  [[nodiscard]] auto Readable() const noexcept -> std::size_t {
    const auto write = write_.load(std::memory_order_acquire);
    const auto read = read_.load(std::memory_order_relaxed);
    return write >= read ? std::min(write - read, capacity_) : 0U;
  }

  [[nodiscard]] auto Writable() const noexcept -> std::size_t {
    return capacity_ - Readable();
  }

  auto Push(const float* const left, const float* const right,
            const std::size_t count) noexcept -> std::size_t {
    if (left == nullptr || count == 0U || capacity_ == 0U) {
      return 0U;
    }
    const auto write = write_.load(std::memory_order_relaxed);
    const auto read = read_.load(std::memory_order_acquire);
    const auto available = write >= read ? capacity_ - (write - read) : 0U;
    const auto accepted = std::min(count, available);
    for (std::size_t index = 0; index < accepted; ++index) {
      const auto slot = (write + index) % capacity_;
      left_[slot] = left[index];
      right_[slot] = right != nullptr ? right[index] : left[index];
    }
    write_.store(write + accepted, std::memory_order_release);
    return accepted;
  }

  auto Pop(float* const left, float* const right,
           const std::size_t count) noexcept -> std::size_t {
    if (left == nullptr || count == 0U || capacity_ == 0U) {
      return 0U;
    }
    const auto read = read_.load(std::memory_order_relaxed);
    const auto write = write_.load(std::memory_order_acquire);
    const auto available = write >= read ? std::min(write - read, capacity_) : 0U;
    const auto consumed = std::min(count, available);
    for (std::size_t index = 0; index < consumed; ++index) {
      const auto slot = (read + index) % capacity_;
      left[index] = left_[slot];
      if (right != nullptr) {
        right[index] = right_[slot];
      }
    }
    for (std::size_t index = consumed; index < count; ++index) {
      left[index] = 0.0F;
      if (right != nullptr) {
        right[index] = 0.0F;
      }
    }
    read_.store(read + consumed, std::memory_order_release);
    return consumed;
  }

  void Clear() noexcept {
    const auto write = write_.load(std::memory_order_acquire);
    read_.store(write, std::memory_order_release);
  }

 private:
  std::vector<float> left_;
  std::vector<float> right_;
  const std::size_t capacity_;
  std::atomic<std::size_t> read_{0U};
  std::atomic<std::size_t> write_{0U};
};

}  // namespace audio_bridge::audio

#endif
