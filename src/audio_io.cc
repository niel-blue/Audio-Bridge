#include "audio_io.h"

#include <algorithm>
#include <mutex>
#include <utility>

#include "audio_device_ids.h"
#include "legacy_io.h"
#include "wasapi_io.h"

namespace audio_bridge::audio {
namespace {

auto IsLegacyId(const std::string& id) -> bool {
  return device_ids::IsLegacy(id);
}

enum class BackendKind {
  kNone,
  kWasapi,
  kLegacy,
};

auto BackendForId(const std::string& id) -> BackendKind {
  if (id.empty()) {
    return BackendKind::kNone;
  }
  return IsLegacyId(id) ? BackendKind::kLegacy : BackendKind::kWasapi;
}

}  // namespace

class AudioIo::Impl final {
 public:
  explicit Impl(StatusCallback callback)
      : callback_(std::move(callback)),
        wasapi_([this](const Status status, const std::string& error) {
          OnBackendStatus(true, status, error);
        }),
        legacy_([this](const Status status, const std::string& error) {
          OnBackendStatus(false, status, error);
        }) {}

  ~Impl() { Stop(); }

  void Start(const Config& config) {
    Stop();
    bool start_wasapi = false;
    bool start_legacy = false;
    bool use_input_wasapi = false;
    bool use_output_wasapi = false;
    bool use_input_legacy = false;
    bool use_output_legacy = false;
    {
      const auto lock = std::lock_guard(state_mutex_);
      input_backend_ = BackendForId(config.input_id);
      output_backend_ = BackendForId(config.output_id);
      start_wasapi = input_backend_ == BackendKind::kWasapi ||
                     output_backend_ == BackendKind::kWasapi;
      start_legacy = input_backend_ == BackendKind::kLegacy ||
                     output_backend_ == BackendKind::kLegacy;
      use_input_wasapi = input_backend_ == BackendKind::kWasapi;
      use_output_wasapi = output_backend_ == BackendKind::kWasapi;
      use_input_legacy = input_backend_ == BackendKind::kLegacy;
      use_output_legacy = output_backend_ == BackendKind::kLegacy;
      wasapi_status_ = start_wasapi ? Status::kStarting : Status::kOff;
      legacy_status_ = start_legacy ? Status::kStarting : Status::kOff;
      wasapi_error_.clear();
      legacy_error_.clear();
    }
    input_enabled_.store(!config.input_id.empty(), std::memory_order_release);
    output_enabled_.store(!config.output_id.empty(), std::memory_order_release);
    RecomputeStatus();

    auto wasapi_config = Config{};
    wasapi_config.input_id =
        use_input_wasapi ? device_ids::WasapiPayload(config.input_id) : "";
    wasapi_config.output_id =
        use_output_wasapi ? device_ids::WasapiPayload(config.output_id) : "";
    wasapi_config.exclusive = config.exclusive;
    wasapi_config.host_sample_rate = config.host_sample_rate;
    wasapi_config.input_loopback =
        use_input_wasapi && device_ids::IsWasapiLoopback(config.input_id);

    auto legacy_config = Config{};
    legacy_config.input_id = use_input_legacy ? config.input_id : "";
    legacy_config.output_id = use_output_legacy ? config.output_id : "";
    legacy_config.exclusive = false;
    legacy_config.host_sample_rate = config.host_sample_rate;

    if (start_wasapi) {
      wasapi_.Start(wasapi_config);
    }
    if (start_legacy) {
      legacy_.Start(legacy_config);
    }
    RecomputeStatus();
  }

  void Stop() noexcept {
    wasapi_.Stop();
    legacy_.Stop();
    input_enabled_.store(false, std::memory_order_release);
    output_enabled_.store(false, std::memory_order_release);
    {
      const auto lock = std::lock_guard(state_mutex_);
      input_backend_ = BackendKind::kNone;
      output_backend_ = BackendKind::kNone;
      wasapi_status_ = Status::kOff;
      legacy_status_ = Status::kOff;
      wasapi_error_.clear();
      legacy_error_.clear();
    }
    RecomputeStatus();
  }

  void PushOutputBlock(const float* const left, const float* const right,
                       const std::size_t count) noexcept {
    wasapi_.PushOutputBlock(left, right, count);
    legacy_.PushOutputBlock(left, right, count);
  }

  void PopInputBlock(float* const left, float* const right,
                     const std::size_t count) noexcept {
    bool use_wasapi = false;
    bool use_legacy = false;
    {
      const auto lock = std::lock_guard(state_mutex_);
      use_wasapi = input_backend_ == BackendKind::kWasapi;
      use_legacy = input_backend_ == BackendKind::kLegacy;
    }
    if (use_wasapi) {
      wasapi_.PopInputBlock(left, right, count);
    } else if (use_legacy) {
      legacy_.PopInputBlock(left, right, count);
    } else {
      if (left != nullptr) {
        std::fill_n(left, count, 0.0F);
      }
      if (right != nullptr) {
        std::fill_n(right, count, 0.0F);
      }
    }
  }

  [[nodiscard]] auto InputEnabled() const noexcept -> bool {
    return input_enabled_.load(std::memory_order_acquire);
  }
  [[nodiscard]] auto OutputEnabled() const noexcept -> bool {
    return output_enabled_.load(std::memory_order_acquire);
  }
  [[nodiscard]] auto GetStatus() const noexcept -> Status {
    return status_.load(std::memory_order_acquire);
  }

 private:
  void OnBackendStatus(const bool wasapi, const Status status,
                       const std::string& error) {
    {
      const auto lock = std::lock_guard(state_mutex_);
      if (wasapi) {
        wasapi_status_ = status;
        wasapi_error_ = error;
      } else {
        legacy_status_ = status;
        legacy_error_ = error;
      }
    }
    RecomputeStatus();
  }

  void RecomputeStatus() {
    auto next = Status::kOff;
    auto error = std::string{};
    {
      const auto lock = std::lock_guard(state_mutex_);
      const auto selected_wasapi =
          input_backend_ == BackendKind::kWasapi ||
          output_backend_ == BackendKind::kWasapi;
      const auto selected_legacy =
          input_backend_ == BackendKind::kLegacy ||
          output_backend_ == BackendKind::kLegacy;
      if (selected_wasapi || selected_legacy) {
        const auto unavailable =
            (selected_wasapi && wasapi_status_ == Status::kUnavailable) ||
            (selected_legacy && legacy_status_ == Status::kUnavailable);
        const auto starting =
            (selected_wasapi && wasapi_status_ == Status::kStarting) ||
            (selected_legacy && legacy_status_ == Status::kStarting);
        const auto active =
            (!selected_wasapi || wasapi_status_ == Status::kActive) &&
            (!selected_legacy || legacy_status_ == Status::kActive);
        if (unavailable) {
          next = Status::kUnavailable;
          error = !wasapi_error_.empty() ? wasapi_error_ : legacy_error_;
        } else if (starting || !active) {
          next = Status::kStarting;
        } else {
          next = Status::kActive;
        }
      }
    }
    const auto changed = status_.load(std::memory_order_acquire) != next;
    status_.store(next, std::memory_order_release);
    if (changed || next == Status::kUnavailable) {
      if (callback_) {
        callback_(next, error);
      }
    }
  }

  StatusCallback callback_;
  WasapiIo wasapi_;
  LegacyIo legacy_;
  mutable std::mutex state_mutex_;
  std::atomic<Status> status_{Status::kOff};
  std::atomic<bool> input_enabled_{false};
  std::atomic<bool> output_enabled_{false};
  BackendKind input_backend_ = BackendKind::kNone;
  BackendKind output_backend_ = BackendKind::kNone;
  Status wasapi_status_ = Status::kOff;
  Status legacy_status_ = Status::kOff;
  std::string wasapi_error_;
  std::string legacy_error_;
};

AudioIo::AudioIo(StatusCallback callback)
    : impl_(std::make_unique<Impl>(std::move(callback))) {}
AudioIo::~AudioIo() = default;
void AudioIo::Start(const Config& config) { impl_->Start(config); }
void AudioIo::Stop() noexcept { impl_->Stop(); }
void AudioIo::PushOutputBlock(const float* left, const float* right,
                              const std::size_t count) noexcept {
  impl_->PushOutputBlock(left, right, count);
}
void AudioIo::PopInputBlock(float* left, float* right,
                            const std::size_t count) noexcept {
  impl_->PopInputBlock(left, right, count);
}
auto AudioIo::InputEnabled() const noexcept -> bool {
  return impl_->InputEnabled();
}
auto AudioIo::OutputEnabled() const noexcept -> bool {
  return impl_->OutputEnabled();
}
auto AudioIo::GetStatus() const noexcept -> Status {
  return impl_->GetStatus();
}

}  // namespace audio_bridge::audio
