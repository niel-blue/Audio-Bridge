#include "wasapi_io.h"

#include "audio_device_ids.h"
#include "audio_fifo.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <Audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

namespace audio_bridge::audio {
namespace {

using Microsoft::WRL::ComPtr;
constexpr auto kFifoCapacity = std::size_t{1U << 16};
constexpr auto kEventFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
constexpr auto kDriftControlInterval = std::size_t{128U};
constexpr auto kMaximumDriftCorrection = 0.001;
constexpr auto kDriftControllerGain = 0.001;
constexpr auto kTargetInputPeriods = 3U;
constexpr auto kTargetOutputPeriods = 2U;

struct CoTaskMemDeleter {
  void operator()(WAVEFORMATEX* const value) const noexcept {
    CoTaskMemFree(value);
  }
};
using WaveFormatPtr = std::unique_ptr<WAVEFORMATEX, CoTaskMemDeleter>;

auto Utf8ToWide(const std::string& text) -> std::wstring {
  if (text.empty()) {
    return {};
  }
  const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        text.data(),
                                        static_cast<int>(text.size()), nullptr,
                                        0);
  if (size <= 0) {
    return {};
  }
  auto result = std::wstring(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                      static_cast<int>(text.size()), result.data(), size);
  return result;
}

auto HResultText(const char* const operation, const HRESULT result)
    -> std::string {
  auto stream = std::ostringstream{};
  stream << operation << " failed (0x" << std::uppercase << std::hex
         << static_cast<unsigned long>(result) << ").";
  return stream.str();
}

enum class SampleEncoding {
  kUnsupported,
  kFloat32,
  kPcm8,
  kPcm16,
  kPcm24,
  kPcm32,
};

auto IsExtensibleSubtype(const WAVEFORMATEX& format, const GUID& subtype)
    -> bool {
  if (format.wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
      format.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
    return false;
  }
  const auto& extensible =
      reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
  return extensible.SubFormat == subtype;
}

auto EncodingFor(const WAVEFORMATEX& format) -> SampleEncoding {
  if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
    return format.wBitsPerSample == 32 ? SampleEncoding::kFloat32
                                       : SampleEncoding::kUnsupported;
  }
  if (IsExtensibleSubtype(format, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
    return format.wBitsPerSample == 32 ? SampleEncoding::kFloat32
                                       : SampleEncoding::kUnsupported;
  }
  if (format.wFormatTag != WAVE_FORMAT_PCM &&
      !IsExtensibleSubtype(format, KSDATAFORMAT_SUBTYPE_PCM)) {
    return SampleEncoding::kUnsupported;
  }
  switch (format.wBitsPerSample) {
    case 8:
      return SampleEncoding::kPcm8;
    case 16:
      return SampleEncoding::kPcm16;
    case 24:
      return SampleEncoding::kPcm24;
    case 32:
      return SampleEncoding::kPcm32;
    default:
      return SampleEncoding::kUnsupported;
  }
}

auto SupportsFormat(IAudioClient* const client, const AUDCLNT_SHAREMODE mode,
                    WAVEFORMATEX& format) -> bool {
  WAVEFORMATEX* closest = nullptr;
  const auto result = client->IsFormatSupported(mode, &format, &closest);
  CoTaskMemFree(closest);
  return result == S_OK;
}

auto MakeRequestedFormat(const double sample_rate) -> WAVEFORMATEX {
  const auto rate = static_cast<DWORD>(std::clamp(sample_rate, 8000.0,
                                                  192000.0));
  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
  format.nChannels = 2;
  format.nSamplesPerSec = rate;
  format.wBitsPerSample = 32;
  format.nBlockAlign = static_cast<WORD>(format.nChannels * sizeof(float));
  format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
  return format;
}

auto InitializeSharedClient(IAudioClient* const client,
                            WAVEFORMATEX& format, UINT32& period_frames)
    -> HRESULT {
  ComPtr<IAudioClient3> low_latency_client;
  if (SUCCEEDED(client->QueryInterface(IID_PPV_ARGS(&low_latency_client)))) {
    UINT32 default_period = 0;
    UINT32 fundamental_period = 0;
    UINT32 minimum_period = 0;
    UINT32 maximum_period = 0;
    auto result = low_latency_client->GetSharedModeEnginePeriod(
        &format, &default_period, &fundamental_period, &minimum_period,
        &maximum_period);
    if (SUCCEEDED(result)) {
      period_frames = fundamental_period == 0
                          ? minimum_period
                          : ((minimum_period + fundamental_period - 1) /
                             fundamental_period) * fundamental_period;
      if (period_frames == 0U || period_frames > maximum_period) {
        period_frames = default_period;
      }
      if (period_frames > 0U && period_frames <= maximum_period) {
        result = low_latency_client->InitializeSharedAudioStream(
            kEventFlags, period_frames, &format, nullptr);
        if (SUCCEEDED(result)) {
          return result;
        }
      }
    }
  }
  const auto result = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                         kEventFlags, 0, 0, &format, nullptr);
  if (SUCCEEDED(result)) {
    REFERENCE_TIME device_period = 0;
    if (SUCCEEDED(client->GetDevicePeriod(&device_period, nullptr))) {
      period_frames = static_cast<UINT32>(std::max<REFERENCE_TIME>(
          1, (device_period * format.nSamplesPerSec + 9999999) / 10000000));
    }
  }
  return result;
}

auto InitializeLoopbackClient(IAudioClient* const client,
                              WAVEFORMATEX& format, UINT32& period_frames)
    -> HRESULT {
  const auto flags = static_cast<DWORD>(kEventFlags |
                                        AUDCLNT_STREAMFLAGS_LOOPBACK);
  const auto result = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0,
                                         &format, nullptr);
  if (SUCCEEDED(result)) {
    REFERENCE_TIME device_period = 0;
    if (SUCCEEDED(client->GetDevicePeriod(&device_period, nullptr))) {
      period_frames = static_cast<UINT32>(std::max<REFERENCE_TIME>(
          1, (device_period * format.nSamplesPerSec + 9999999) / 10000000));
    }
  }
  return result;
}

auto InitializeExclusiveClient(IAudioClient* const client,
                               WAVEFORMATEX& format, UINT32& period_frames)
    -> HRESULT {
  REFERENCE_TIME default_period = 0;
  REFERENCE_TIME minimum_period = 0;
  auto result = client->GetDevicePeriod(&default_period, &minimum_period);
  if (FAILED(result)) {
    return result;
  }
  auto period = minimum_period > 0 ? minimum_period : default_period;
  if (period <= 0 || format.nSamplesPerSec == 0U) {
    return AUDCLNT_E_BUFDURATION_PERIOD_NOT_EQUAL;
  }
  period_frames = static_cast<UINT32>(std::max<REFERENCE_TIME>(
      1, (period * format.nSamplesPerSec + 9999999) / 10000000));
  result = client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, kEventFlags,
                              period, period, &format, nullptr);
  if (FAILED(result) && default_period > 0 && default_period != period) {
    period = default_period;
    period_frames = static_cast<UINT32>(std::max<REFERENCE_TIME>(
        1, (period * format.nSamplesPerSec + 9999999) / 10000000));
    result = client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, kEventFlags,
                                period, period, &format, nullptr);
  }
  return result;
}

void ReadFrame(const BYTE* const data, const UINT32 frame,
               const WAVEFORMATEX& format, float& left, float& right) noexcept {
  if (data == nullptr || format.nChannels == 0U) {
    left = 0.0F;
    right = 0.0F;
    return;
  }
  const auto encoding = EncodingFor(format);
  const auto bytes_per_sample =
      static_cast<std::size_t>(format.nBlockAlign) / format.nChannels;
  if (bytes_per_sample == 0U) {
    left = 0.0F;
    right = 0.0F;
    return;
  }
  const auto* const frame_data =
      data + static_cast<std::size_t>(frame) * format.nBlockAlign;
  const auto read_channel = [encoding, bytes_per_sample,
                             frame_data](const UINT32 channel) -> float {
    const auto* const sample = frame_data + channel * bytes_per_sample;
    switch (encoding) {
      case SampleEncoding::kFloat32: {
        auto value = 0.0F;
        std::memcpy(&value, sample, sizeof(value));
        return value;
      }
      case SampleEncoding::kPcm8:
        return (static_cast<float>(sample[0]) - 128.0F) / 128.0F;
      case SampleEncoding::kPcm16: {
        auto value = std::int16_t{};
        std::memcpy(&value, sample, sizeof(value));
        return static_cast<float>(value) / 32768.0F;
      }
      case SampleEncoding::kPcm24: {
        auto value = static_cast<std::int32_t>(sample[0]) |
                     (static_cast<std::int32_t>(sample[1]) << 8) |
                     (static_cast<std::int32_t>(sample[2]) << 16);
        if ((value & 0x00800000) != 0) {
          value |= static_cast<std::int32_t>(0xFF000000);
        }
        return static_cast<float>(value) / 8388608.0F;
      }
      case SampleEncoding::kPcm32: {
        auto value = std::int32_t{};
        std::memcpy(&value, sample, sizeof(value));
        return static_cast<float>(static_cast<double>(value) / 2147483648.0);
      }
      case SampleEncoding::kUnsupported:
        return 0.0F;
    }
    return 0.0F;
  };
  left = read_channel(0U);
  right = format.nChannels > 1U ? read_channel(1U) : left;
}

void WriteFrame(BYTE* const data, const UINT32 frame,
                const WAVEFORMATEX& format, const float left,
                const float right) noexcept {
  if (data == nullptr || format.nChannels == 0U) {
    return;
  }
  const auto encoding = EncodingFor(format);
  const auto bytes_per_sample =
      static_cast<std::size_t>(format.nBlockAlign) / format.nChannels;
  if (bytes_per_sample == 0U) {
    return;
  }
  const auto mono = (left + right) * 0.5F;
  auto* const frame_data =
      data + static_cast<std::size_t>(frame) * format.nBlockAlign;
  for (auto channel = 0U; channel < format.nChannels; ++channel) {
    const auto value = format.nChannels == 1U
                           ? mono
                           : (channel == 0U ? left
                                            : (channel == 1U ? right : 0.0F));
    auto* const sample = frame_data + channel * bytes_per_sample;
    const auto clipped = std::clamp(value, -1.0F, 1.0F);
    switch (encoding) {
      case SampleEncoding::kFloat32:
        std::memcpy(sample, &clipped, sizeof(clipped));
        break;
      case SampleEncoding::kPcm8: {
        const auto encoded = static_cast<BYTE>(std::clamp(
            std::lrint(clipped * 127.0F) + 128L, 0L, 255L));
        sample[0] = encoded;
        break;
      }
      case SampleEncoding::kPcm16: {
        const auto encoded = static_cast<std::int16_t>(
            clipped <= -1.0F ? -32768 : std::lrint(clipped * 32767.0F));
        std::memcpy(sample, &encoded, sizeof(encoded));
        break;
      }
      case SampleEncoding::kPcm24: {
        const auto encoded = static_cast<std::int32_t>(
            clipped <= -1.0F ? -8388608 : std::lrint(clipped * 8388607.0F));
        sample[0] = static_cast<BYTE>(encoded & 0xFF);
        sample[1] = static_cast<BYTE>((encoded >> 8) & 0xFF);
        sample[2] = static_cast<BYTE>((encoded >> 16) & 0xFF);
        break;
      }
      case SampleEncoding::kPcm32: {
        const auto encoded = static_cast<std::int32_t>(
            clipped <= -1.0F
                ? std::numeric_limits<std::int32_t>::min()
                : std::llround(static_cast<double>(clipped) * 2147483647.0));
        std::memcpy(sample, &encoded, sizeof(encoded));
        break;
      }
      case SampleEncoding::kUnsupported:
        break;
    }
  }
}

// Reads samples across independently clocked devices. The source FIFO contains
// the samples being converted, while the feedback FIFO indicates whether the
// destination is gaining or losing buffered audio. For output they are the
// same FIFO; for input they are the raw capture and host-rate FIFOs.
class AdaptiveReader final {
 public:
  AdaptiveReader(StereoFifo& source_fifo, StereoFifo& feedback_fifo,
                 const double source_rate, const double destination_rate,
                 const std::size_t target_fill)
      : source_fifo_(source_fifo),
        feedback_fifo_(feedback_fifo),
        nominal_ratio_(source_rate > 0.0 && destination_rate > 0.0
                           ? source_rate / destination_rate
                           : 1.0),
        ratio_(nominal_ratio_),
        target_fill_(std::max<std::size_t>(2U, target_fill)) {}

  void Next(float& left, float& right) noexcept {
    UpdateDriftCorrection();
    if (!initialized_) {
      static_cast<void>(
          source_fifo_.Pop(&previous_left_, &previous_right_, 1U));
      static_cast<void>(source_fifo_.Pop(&next_left_, &next_right_, 1U));
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
      static_cast<void>(source_fifo_.Pop(&next_left_, &next_right_, 1U));
      phase_ -= 1.0;
    }
  }

  void Reset() noexcept {
    ratio_ = nominal_ratio_;
    control_counter_ = 0U;
    phase_ = 0.0;
    previous_left_ = 0.0F;
    previous_right_ = 0.0F;
    next_left_ = 0.0F;
    next_right_ = 0.0F;
    initialized_ = false;
  }

  [[nodiscard]] auto RequiredSourceFrames() const noexcept -> std::size_t {
    return 2U + static_cast<std::size_t>(
                    std::ceil(nominal_ratio_ *
                              (1.0 + kMaximumDriftCorrection)));
  }

 private:
  void UpdateDriftCorrection() noexcept {
    if (++control_counter_ < kDriftControlInterval) {
      return;
    }
    control_counter_ = 0U;

    const auto readable = static_cast<double>(feedback_fifo_.Readable());
    const auto target = static_cast<double>(target_fill_);
    const auto normalized_error = (readable - target) / target;
    const auto correction = std::clamp(
        normalized_error * kDriftControllerGain,
        -kMaximumDriftCorrection, kMaximumDriftCorrection);
    ratio_ = nominal_ratio_ * (1.0 + correction);
  }

  StereoFifo& source_fifo_;
  StereoFifo& feedback_fifo_;
  double nominal_ratio_ = 1.0;
  double ratio_ = 1.0;
  std::size_t target_fill_ = 2U;
  std::size_t control_counter_ = 0U;
  double phase_ = 0.0;
  float previous_left_ = 0.0F;
  float previous_right_ = 0.0F;
  float next_left_ = 0.0F;
  float next_right_ = 0.0F;
  bool initialized_ = false;
};

struct Endpoint {
  ComPtr<IAudioClient> client;
  WaveFormatPtr format;
  UINT32 buffer_frames = 0U;
  UINT32 period_frames = 0U;
  double sample_rate = 0.0;
};

struct EndpointDevice {
  ComPtr<IMMDevice> device;
  std::wstring id;
};

enum class EndpointRole {
  kCapture,
  kLoopbackCapture,
  kRender,
};

auto ResolveEndpointDevice(IMMDeviceEnumerator* const enumerator,
                           const std::string& device_id,
                           const EDataFlow direction,
                           EndpointDevice& endpoint) -> std::string {
  if (device_id.empty()) {
    return direction == eCapture ? "No WASAPI input device was selected."
                                 : "No WASAPI output device was selected.";
  }
  auto result = device_id == device_ids::kDefaultWasapiPayload
                    ? enumerator->GetDefaultAudioEndpoint(
                          direction, eConsole, &endpoint.device)
                    : E_INVALIDARG;
  if (device_id != device_ids::kDefaultWasapiPayload) {
    const auto wide_id = Utf8ToWide(device_id);
    if (wide_id.empty()) {
      return "The selected WASAPI device ID is invalid.";
    }
    result = enumerator->GetDevice(wide_id.c_str(), &endpoint.device);
  }
  if (FAILED(result)) {
    return HResultText(direction == eCapture ? "WASAPI input device lookup"
                                             : "WASAPI output device lookup",
                       result);
  }
  LPWSTR resolved_id = nullptr;
  result = endpoint.device->GetId(&resolved_id);
  if (FAILED(result) || resolved_id == nullptr) {
    CoTaskMemFree(resolved_id);
    return HResultText("WASAPI device ID query", result);
  }
  endpoint.id = resolved_id;
  CoTaskMemFree(resolved_id);
  return {};
}

auto OpenEndpoint(const EndpointDevice& device, const EndpointRole role,
                  const bool requested_exclusive, const double host_rate,
                  HANDLE event_handle, Endpoint& endpoint) -> std::string {
  const auto loopback = role == EndpointRole::kLoopbackCapture;
  const auto exclusive = requested_exclusive && !loopback;
  auto result = device.device->Activate(
      __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
      reinterpret_cast<void**>(endpoint.client.GetAddressOf()));
  if (FAILED(result)) {
    return HResultText("WASAPI audio client activation", result);
  }

  const auto mode = exclusive ? AUDCLNT_SHAREMODE_EXCLUSIVE
                              : AUDCLNT_SHAREMODE_SHARED;
  if (loopback) {
    WAVEFORMATEX* raw = nullptr;
    result = endpoint.client->GetMixFormat(&raw);
    endpoint.format.reset(raw);
    if (FAILED(result) || endpoint.format == nullptr) {
      return HResultText("WASAPI mix format", result);
    }
  } else {
    auto requested = MakeRequestedFormat(host_rate);
    if (SupportsFormat(endpoint.client.Get(), mode, requested)) {
      const auto bytes = sizeof(WAVEFORMATEX) + requested.cbSize;
      auto* const raw = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(bytes));
      if (raw == nullptr) {
        return "WASAPI format allocation failed.";
      }
      std::memcpy(raw, &requested, bytes);
      endpoint.format.reset(raw);
    } else {
      WAVEFORMATEX* raw = nullptr;
      result = endpoint.client->GetMixFormat(&raw);
      endpoint.format.reset(raw);
      if (FAILED(result) || endpoint.format == nullptr) {
        return HResultText("WASAPI mix format", result);
      }
    }
  }
  if (EncodingFor(*endpoint.format) == SampleEncoding::kUnsupported) {
    return "The selected WASAPI device uses an unsupported format.";
  }

  if (exclusive && !SupportsFormat(endpoint.client.Get(), mode,
                                    *endpoint.format)) {
    return "The selected device does not support its selected format in exclusive mode.";
  }
  if (loopback) {
    result = InitializeLoopbackClient(endpoint.client.Get(), *endpoint.format,
                                      endpoint.period_frames);
  } else if (exclusive) {
    result = InitializeExclusiveClient(endpoint.client.Get(), *endpoint.format,
                                       endpoint.period_frames);
  } else {
    result = InitializeSharedClient(endpoint.client.Get(), *endpoint.format,
                                    endpoint.period_frames);
  }
  if (FAILED(result)) {
    return HResultText("WASAPI stream initialization", result);
  }
  result = endpoint.client->SetEventHandle(event_handle);
  if (FAILED(result)) {
    return HResultText("WASAPI event setup", result);
  }
  result = endpoint.client->GetBufferSize(&endpoint.buffer_frames);
  if (FAILED(result) || endpoint.buffer_frames == 0U) {
    return HResultText("WASAPI buffer query", result);
  }
  endpoint.sample_rate = static_cast<double>(endpoint.format->nSamplesPerSec);
  return {};
}

auto TargetFillAtHostRate(const Endpoint& endpoint, const double host_rate,
                          const unsigned periods)
    -> std::size_t {
  if (endpoint.period_frames == 0U || endpoint.sample_rate <= 0.0 ||
      host_rate <= 0.0) {
    return 512U;
  }
  const auto target = static_cast<double>(endpoint.period_frames) *
                      static_cast<double>(periods) * host_rate /
                      endpoint.sample_rate;
  return std::clamp<std::size_t>(
      static_cast<std::size_t>(std::ceil(target)), 256U, kFifoCapacity / 4U);
}

}  // namespace

class WasapiIo::Impl final {
 public:
  explicit Impl(StatusCallback callback)
      : callback_(std::move(callback)),
        input_raw_(kFifoCapacity),
        input_host_(kFifoCapacity),
        output_host_(kFifoCapacity) {}

  ~Impl() { Stop(); }

  void Start(const Config& config) {
    Stop();
    input_enabled_.store(!config.input_id.empty(), std::memory_order_release);
    output_enabled_.store(!config.output_id.empty(), std::memory_order_release);
    if (!input_enabled_.load(std::memory_order_acquire) &&
        !output_enabled_.load(std::memory_order_acquire)) {
      return;
    }
    input_raw_.Clear();
    input_host_.Clear();
    output_host_.Clear();
    input_target_fill_.store(0U, std::memory_order_release);
    input_overrun_.store(false, std::memory_order_release);
    input_reset_requested_.store(false, std::memory_order_release);
    input_primed_.store(false, std::memory_order_release);
    config_ = config;
    status_.store(Status::kStarting, std::memory_order_release);
    Notify(Status::kStarting, {});
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    input_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    output_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (stop_event_ == nullptr || input_event_ == nullptr ||
        output_event_ == nullptr) {
      CloseEvents();
      SetUnavailable("WASAPI event creation failed.");
      return;
    }
    thread_ = std::thread([this]() { Run(); });
  }

  void Stop() noexcept {
    if (stop_event_ != nullptr) {
      SetEvent(stop_event_);
    }
    if (thread_.joinable()) {
      thread_.join();
    }
    CloseEvents();
    input_enabled_.store(false, std::memory_order_release);
    output_enabled_.store(false, std::memory_order_release);
    input_raw_.Clear();
    input_host_.Clear();
    output_host_.Clear();
    input_target_fill_.store(0U, std::memory_order_release);
    input_overrun_.store(false, std::memory_order_release);
    input_reset_requested_.store(false, std::memory_order_release);
    input_primed_.store(false, std::memory_order_release);
    if (status_.load(std::memory_order_acquire) != Status::kOff) {
      status_.store(Status::kOff, std::memory_order_release);
      Notify(Status::kOff, {});
    }
  }

  void PushOutputBlock(const float* const left, const float* const right,
                       const std::size_t count) noexcept {
    if (output_enabled_.load(std::memory_order_relaxed)) {
      static_cast<void>(output_host_.Push(left, right, count));
    }
  }

  void PopInputBlock(float* const left, float* const right,
                     const std::size_t count) noexcept {
    const auto write_silence = [left, right, count]() {
      if (left == nullptr) {
        return;
      }
      std::fill_n(left, count, 0.0F);
      if (right != nullptr) {
        std::fill_n(right, count, 0.0F);
      }
    };
    if (!input_enabled_.load(std::memory_order_relaxed) || left == nullptr) {
      write_silence();
      return;
    }

    const auto generation =
        input_pipeline_generation_.load(std::memory_order_acquire);
    if (generation != input_seen_generation_) {
      input_seen_generation_ = generation;
      input_host_.Clear();
      input_primed_.store(false, std::memory_order_release);
    }
    if (input_reset_requested_.load(std::memory_order_acquire)) {
      write_silence();
      return;
    }

    const auto target = std::max<std::size_t>(
        2U, input_target_fill_.load(std::memory_order_acquire));
    const auto readable = input_host_.Readable();
    const auto stale_threshold = std::min<std::size_t>(
        kFifoCapacity / 2U,
        std::max({target * 4U, count * 4U, std::size_t{1024U}}));
    if (input_overrun_.exchange(false, std::memory_order_acq_rel) ||
        readable > stale_threshold) {
      input_host_.Clear();
      input_reset_requested_.store(true, std::memory_order_release);
      input_primed_.store(false, std::memory_order_release);
      write_silence();
      return;
    }

    const auto required = std::max(target, count);
    if (!input_primed_.load(std::memory_order_acquire)) {
      if (readable < required) {
        write_silence();
        return;
      }
      input_primed_.store(true, std::memory_order_release);
    }
    if (input_host_.Readable() < count) {
      input_primed_.store(false, std::memory_order_release);
      write_silence();
      return;
    }
    static_cast<void>(input_host_.Pop(left, right, count));
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
  void CloseEvents() noexcept {
    if (stop_event_ != nullptr) {
      CloseHandle(stop_event_);
      stop_event_ = nullptr;
    }
    if (input_event_ != nullptr) {
      CloseHandle(input_event_);
      input_event_ = nullptr;
    }
    if (output_event_ != nullptr) {
      CloseHandle(output_event_);
      output_event_ = nullptr;
    }
  }

  void Notify(const Status status, const std::string& error) {
    if (callback_) {
      callback_(status, error);
    }
  }

  void SetUnavailable(const std::string& error) {
    status_.store(Status::kUnavailable, std::memory_order_release);
    Notify(Status::kUnavailable, error);
  }

  void CaptureAvailable(IAudioCaptureClient* const capture,
                        const WAVEFORMATEX& format) {
    ResetInputPipelineIfRequested();
    UINT32 packet_frames = 0U;
    while (SUCCEEDED(capture->GetNextPacketSize(&packet_frames)) &&
           packet_frames > 0U) {
      BYTE* data = nullptr;
      UINT32 frames = 0U;
      DWORD flags = 0U;
      auto result = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
      if (FAILED(result)) {
        SetUnavailable(HResultText("WASAPI capture buffer", result));
        return;
      }
      if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0U) {
        data = nullptr;
      }
      for (UINT32 frame = 0; frame < frames; ++frame) {
        float left = 0.0F;
        float right = 0.0F;
        ReadFrame(data, frame, format, left, right);
        if (input_raw_.Push(&left, &right, 1U) != 1U) {
          input_overrun_.store(true, std::memory_order_release);
        }
      }
      static_cast<void>(capture->ReleaseBuffer(frames));
    }
    ConvertInput();
  }

  void ConvertInput() {
    if (input_resampler_ == nullptr) {
      return;
    }
    const auto required = input_resampler_->RequiredSourceFrames();
    while (input_host_.Writable() > 0U &&
           input_raw_.Readable() >= required) {
      float left = 0.0F;
      float right = 0.0F;
      input_resampler_->Next(left, right);
      static_cast<void>(input_host_.Push(&left, &right, 1U));
    }
  }

  void ResetInputPipelineIfRequested() {
    if (!input_reset_requested_.load(std::memory_order_acquire)) {
      return;
    }
    input_raw_.Clear();
    if (input_resampler_ != nullptr) {
      input_resampler_->Reset();
    }
    input_pipeline_generation_.fetch_add(1U, std::memory_order_release);
    input_reset_requested_.store(false, std::memory_order_release);
  }

  auto FillOutput(IAudioRenderClient* const render,
                  const WAVEFORMATEX& format, const UINT32 frames) -> HRESULT {
    BYTE* data = nullptr;
    auto result = render->GetBuffer(frames, &data);
    if (FAILED(result)) {
      return result;
    }
    for (UINT32 frame = 0; frame < frames; ++frame) {
      float left = 0.0F;
      float right = 0.0F;
      if (output_resampler_ != nullptr) {
        output_resampler_->Next(left, right);
      }
      WriteFrame(data, frame, format, left, right);
    }
    return render->ReleaseBuffer(frames, 0U);
  }

  void Run() {
    const auto com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const auto uninitialize = SUCCEEDED(com_result);
    if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE) {
      SetUnavailable("WASAPI COM initialization failed.");
      return;
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    auto result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                   CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(result)) {
      SetUnavailable(HResultText("WASAPI device enumeration", result));
      if (uninitialize) {
        CoUninitialize();
      }
      return;
    }

    EndpointDevice input_device;
    EndpointDevice output_device;
    Endpoint input;
    Endpoint output;
    auto error = std::string{};
    if (InputEnabled()) {
      error = ResolveEndpointDevice(
          enumerator.Get(), config_.input_id,
          config_.input_loopback ? eRender : eCapture, input_device);
    }
    if (error.empty() && OutputEnabled()) {
      error = ResolveEndpointDevice(enumerator.Get(), config_.output_id,
                                    eRender, output_device);
    }
    if (error.empty() && InputEnabled() && OutputEnabled() &&
        config_.input_loopback && input_device.id == output_device.id) {
      error = "The WASAPI loopback source and output device must be different to prevent audio feedback.";
    }
    if (error.empty() && InputEnabled()) {
      error = OpenEndpoint(
          input_device,
          config_.input_loopback ? EndpointRole::kLoopbackCapture
                                 : EndpointRole::kCapture,
          config_.exclusive, config_.host_sample_rate, input_event_, input);
    }
    if (error.empty() && OutputEnabled()) {
      error = OpenEndpoint(output_device, EndpointRole::kRender,
                           config_.exclusive, config_.host_sample_rate,
                           output_event_, output);
    }
    if (!error.empty()) {
      SetUnavailable(error);
      if (uninitialize) {
        CoUninitialize();
      }
      return;
    }

    ComPtr<IAudioCaptureClient> capture;
    ComPtr<IAudioRenderClient> render;
    if (InputEnabled()) {
      result = input.client->GetService(IID_PPV_ARGS(&capture));
      if (FAILED(result)) {
        SetUnavailable(HResultText("WASAPI capture client", result));
        if (uninitialize) {
          CoUninitialize();
        }
        return;
      }
      const auto target = TargetFillAtHostRate(
          input, config_.host_sample_rate, kTargetInputPeriods);
      input_resampler_ = std::make_unique<AdaptiveReader>(
          input_raw_, input_host_, input.sample_rate,
          config_.host_sample_rate, target);
      input_target_fill_.store(target, std::memory_order_release);
      input_pipeline_generation_.fetch_add(1U, std::memory_order_release);
    }
    if (OutputEnabled()) {
      result = output.client->GetService(IID_PPV_ARGS(&render));
      if (FAILED(result)) {
        SetUnavailable(HResultText("WASAPI render client", result));
        if (uninitialize) {
          CoUninitialize();
        }
        return;
      }
      output_resampler_ = std::make_unique<AdaptiveReader>(
          output_host_, output_host_, config_.host_sample_rate,
          output.sample_rate,
          TargetFillAtHostRate(output, config_.host_sample_rate,
                               kTargetOutputPeriods));
      result = FillOutput(render.Get(), *output.format, output.buffer_frames);
      if (FAILED(result)) {
        SetUnavailable(HResultText("WASAPI initial output buffer", result));
        if (uninitialize) {
          CoUninitialize();
        }
        return;
      }
    }

    if (InputEnabled()) {
      result = input.client->Start();
      if (FAILED(result)) {
        SetUnavailable(HResultText("WASAPI capture start", result));
        if (uninitialize) {
          CoUninitialize();
        }
        return;
      }
    }
    if (OutputEnabled()) {
      result = output.client->Start();
      if (FAILED(result)) {
        SetUnavailable(HResultText("WASAPI render start", result));
        if (uninitialize) {
          CoUninitialize();
        }
        return;
      }
    }

    status_.store(Status::kActive, std::memory_order_release);
    Notify(Status::kActive, {});
    auto handles = std::array<HANDLE, 3>{stop_event_, input_event_, output_event_};
    auto running = true;
    while (running) {
      const auto wait_result = WaitForMultipleObjects(
          static_cast<DWORD>(handles.size()), handles.data(), FALSE, INFINITE);
      if (wait_result == WAIT_OBJECT_0) {
        running = false;
        continue;
      }
      if (wait_result == WAIT_OBJECT_0 + 1 && capture != nullptr) {
        CaptureAvailable(capture.Get(), *input.format);
        continue;
      }
      if (wait_result == WAIT_OBJECT_0 + 2 && render != nullptr) {
        UINT32 padding = 0U;
        if (FAILED(output.client->GetCurrentPadding(&padding))) {
          SetUnavailable("WASAPI output padding query failed.");
          running = false;
          continue;
        }
        const auto available = padding < output.buffer_frames
                                   ? output.buffer_frames - padding
                                   : 0U;
        if (available > 0U) {
          result = FillOutput(render.Get(), *output.format, available);
          if (FAILED(result)) {
            SetUnavailable(HResultText("WASAPI output buffer", result));
            running = false;
          }
        }
        continue;
      }
      SetUnavailable("WASAPI event wait failed.");
      running = false;
    }

    if (input.client != nullptr) {
      static_cast<void>(input.client->Stop());
    }
    if (output.client != nullptr) {
      static_cast<void>(output.client->Stop());
    }
    input_resampler_.reset();
    output_resampler_.reset();
    if (uninitialize) {
      CoUninitialize();
    }
  }

  StatusCallback callback_;
  StereoFifo input_raw_;
  StereoFifo input_host_;
  StereoFifo output_host_;
  Config config_;
  std::unique_ptr<AdaptiveReader> input_resampler_;
  std::unique_ptr<AdaptiveReader> output_resampler_;
  std::atomic<Status> status_{Status::kOff};
  std::atomic<bool> input_enabled_{false};
  std::atomic<bool> output_enabled_{false};
  std::atomic<std::size_t> input_target_fill_{0U};
  std::atomic<std::size_t> input_pipeline_generation_{0U};
  std::atomic<bool> input_overrun_{false};
  std::atomic<bool> input_reset_requested_{false};
  std::atomic<bool> input_primed_{false};
  std::size_t input_seen_generation_ = 0U;
  HANDLE stop_event_ = nullptr;
  HANDLE input_event_ = nullptr;
  HANDLE output_event_ = nullptr;
  std::thread thread_;
};

WasapiIo::WasapiIo(StatusCallback callback)
    : impl_(std::make_unique<Impl>(std::move(callback))) {}
WasapiIo::~WasapiIo() = default;
void WasapiIo::Start(const Config& config) { impl_->Start(config); }
void WasapiIo::Stop() noexcept { impl_->Stop(); }
void WasapiIo::PushOutputBlock(const float* left, const float* right,
                               const std::size_t count) noexcept {
  impl_->PushOutputBlock(left, right, count);
}
void WasapiIo::PopInputBlock(float* left, float* right,
                             const std::size_t count) noexcept {
  impl_->PopInputBlock(left, right, count);
}
auto WasapiIo::InputEnabled() const noexcept -> bool {
  return impl_->InputEnabled();
}
auto WasapiIo::OutputEnabled() const noexcept -> bool {
  return impl_->OutputEnabled();
}
auto WasapiIo::GetStatus() const noexcept -> Status {
  return impl_->GetStatus();
}

}  // namespace audio_bridge::audio

#else

namespace audio_bridge::audio {
class WasapiIo::Impl {
 public:
  explicit Impl(StatusCallback) {}
  void Start(const Config&) {}
  void Stop() noexcept {}
  void PushOutputBlock(const float*, const float*, std::size_t) noexcept {}
  void PopInputBlock(float* left, float* right, std::size_t count) noexcept {
    if (left != nullptr) {
      std::fill_n(left, count, 0.0F);
    }
    if (right != nullptr) {
      std::fill_n(right, count, 0.0F);
    }
  }
  [[nodiscard]] auto InputEnabled() const noexcept -> bool { return false; }
  [[nodiscard]] auto OutputEnabled() const noexcept -> bool { return false; }
  [[nodiscard]] auto GetStatus() const noexcept -> Status { return Status::kOff; }
};
WasapiIo::WasapiIo(StatusCallback callback)
    : impl_(std::make_unique<Impl>(std::move(callback))) {}
WasapiIo::~WasapiIo() = default;
void WasapiIo::Start(const Config& config) { impl_->Start(config); }
void WasapiIo::Stop() noexcept { impl_->Stop(); }
void WasapiIo::PushOutputBlock(const float* left, const float* right,
                               std::size_t count) noexcept {
  impl_->PushOutputBlock(left, right, count);
}
void WasapiIo::PopInputBlock(float* left, float* right,
                             std::size_t count) noexcept {
  impl_->PopInputBlock(left, right, count);
}
auto WasapiIo::InputEnabled() const noexcept -> bool {
  return impl_->InputEnabled();
}
auto WasapiIo::OutputEnabled() const noexcept -> bool {
  return impl_->OutputEnabled();
}
auto WasapiIo::GetStatus() const noexcept -> Status { return impl_->GetStatus(); }
}  // namespace audio_bridge::audio

#endif
