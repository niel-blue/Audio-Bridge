#include "wasapi_io.h"

#include "audio_fifo.h"
#include "audio_resampler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

auto IsFloatFormat(const WAVEFORMATEX& format) -> bool {
  if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
    return format.wBitsPerSample == 32;
  }
  if (format.wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
      format.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
    return false;
  }
  const auto& extensible =
      reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
  return extensible.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT &&
         format.wBitsPerSample == 32;
}

auto IsPcm16Format(const WAVEFORMATEX& format) -> bool {
  if (format.wFormatTag == WAVE_FORMAT_PCM) {
    return format.wBitsPerSample == 16;
  }
  if (format.wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
      format.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
    return false;
  }
  const auto& extensible =
      reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
  return extensible.SubFormat == KSDATAFORMAT_SUBTYPE_PCM &&
         format.wBitsPerSample == 16;
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
  const auto offset = static_cast<std::size_t>(frame) * format.nChannels;
  if (IsFloatFormat(format)) {
    const auto* const samples = reinterpret_cast<const float*>(data);
    left = samples[offset];
    right = format.nChannels > 1U ? samples[offset + 1U] : left;
  } else {
    const auto* const samples = reinterpret_cast<const std::int16_t*>(data);
    left = static_cast<float>(samples[offset]) / 32768.0F;
    right = format.nChannels > 1U
                ? static_cast<float>(samples[offset + 1U]) / 32768.0F
                : left;
  }
}

void WriteFrame(BYTE* const data, const UINT32 frame,
                const WAVEFORMATEX& format, const float left,
                const float right) noexcept {
  if (data == nullptr || format.nChannels == 0U) {
    return;
  }
  const auto mono = (left + right) * 0.5F;
  const auto offset = static_cast<std::size_t>(frame) * format.nChannels;
  if (IsFloatFormat(format)) {
    auto* const samples = reinterpret_cast<float*>(data);
    for (auto channel = 0U; channel < format.nChannels; ++channel) {
      samples[offset + channel] = format.nChannels == 1U
                                      ? mono
                                      : (channel == 0U ? left
                                                       : (channel == 1U
                                                              ? right
                                                              : 0.0F));
    }
    return;
  }
  auto* const samples = reinterpret_cast<std::int16_t*>(data);
  for (auto channel = 0U; channel < format.nChannels; ++channel) {
    const auto value = format.nChannels == 1U
                           ? mono
                           : (channel == 0U ? left
                                            : (channel == 1U ? right : 0.0F));
    samples[offset + channel] = static_cast<std::int16_t>(std::lrint(
        std::clamp(value, -1.0F, 1.0F) * 32767.0F));
  }
}

// Reads host-rate samples for a device running on an independent clock.
// The nominal rate conversion handles different sample-rate settings. The
// small feedback correction keeps the FIFO near a target fill level when the
// ASIO host clock and the WASAPI device clock differ by a few parts per
// million. This is intentionally limited so the correction is not audible.
class AdaptiveOutputReader final {
 public:
  AdaptiveOutputReader(StereoFifo& fifo, const double source_rate,
                       const double destination_rate,
                       const std::size_t target_fill)
      : fifo_(fifo),
        nominal_ratio_(source_rate > 0.0 && destination_rate > 0.0
                           ? source_rate / destination_rate
                           : 1.0),
        ratio_(nominal_ratio_),
        target_fill_(std::max<std::size_t>(2U, target_fill)) {}

  void Next(float& left, float& right) noexcept {
    UpdateDriftCorrection();
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
  void UpdateDriftCorrection() noexcept {
    if (++control_counter_ < kDriftControlInterval) {
      return;
    }
    control_counter_ = 0U;

    const auto readable = static_cast<double>(fifo_.Readable());
    const auto target = static_cast<double>(target_fill_);
    const auto normalized_error = (readable - target) / target;
    const auto correction = std::clamp(
        normalized_error * kDriftControllerGain,
        -kMaximumDriftCorrection, kMaximumDriftCorrection);
    ratio_ = nominal_ratio_ * (1.0 + correction);
  }

  StereoFifo& fifo_;
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

auto OpenEndpoint(IMMDeviceEnumerator* const enumerator,
                  const std::string& device_id, const bool capture,
                  const bool exclusive, const double host_rate,
                  HANDLE event_handle, Endpoint& endpoint) -> std::string {
  const auto wide_id = Utf8ToWide(device_id);
  if (wide_id.empty()) {
    return capture ? "No WASAPI input device was selected."
                   : "No WASAPI output device was selected.";
  }
  ComPtr<IMMDevice> device;
  auto result = enumerator->GetDevice(wide_id.c_str(), &device);
  if (FAILED(result)) {
    return HResultText(capture ? "WASAPI input device lookup"
                               : "WASAPI output device lookup",
                       result);
  }
  result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                            reinterpret_cast<void**>(endpoint.client.GetAddressOf()));
  if (FAILED(result)) {
    return HResultText("WASAPI audio client activation", result);
  }

  auto requested = MakeRequestedFormat(host_rate);
  auto mode = exclusive ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED;
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
  if (!IsFloatFormat(*endpoint.format) && !IsPcm16Format(*endpoint.format)) {
    return "The selected WASAPI device uses an unsupported format.";
  }

  if (exclusive && !SupportsFormat(endpoint.client.Get(), mode,
                                    *endpoint.format)) {
    return "The selected device does not support its selected format in exclusive mode.";
  }
  if (exclusive) {
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

auto OutputTargetFill(const Endpoint& endpoint, const double host_rate)
    -> std::size_t {
  if (endpoint.period_frames == 0U || endpoint.sample_rate <= 0.0 ||
      host_rate <= 0.0) {
    return 512U;
  }
  const auto target = static_cast<double>(endpoint.period_frames) *
                      static_cast<double>(kTargetOutputPeriods) * host_rate /
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
    if (input_enabled_.load(std::memory_order_relaxed)) {
      static_cast<void>(input_host_.Pop(left, right, count));
    } else if (left != nullptr) {
      std::fill_n(left, count, 0.0F);
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
        static_cast<void>(input_raw_.Push(&left, &right, 1U));
      }
      static_cast<void>(capture->ReleaseBuffer(frames));
    }
    ConvertInput();
  }

  void ConvertInput() {
    if (input_resampler_ == nullptr) {
      return;
    }
    while (input_host_.Writable() > 0U && input_raw_.Readable() >= 2U) {
      float left = 0.0F;
      float right = 0.0F;
      input_resampler_->Next(left, right);
      static_cast<void>(input_host_.Push(&left, &right, 1U));
    }
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

    Endpoint input;
    Endpoint output;
    auto error = std::string{};
    if (InputEnabled()) {
      error = OpenEndpoint(enumerator.Get(), config_.input_id, true,
                           config_.exclusive, config_.host_sample_rate,
                           input_event_, input);
    }
    if (error.empty() && OutputEnabled()) {
      error = OpenEndpoint(enumerator.Get(), config_.output_id, false,
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
      input_resampler_ = std::make_unique<LinearReader>(
          input_raw_, input.sample_rate, config_.host_sample_rate);
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
      output_resampler_ = std::make_unique<AdaptiveOutputReader>(
          output_host_, config_.host_sample_rate, output.sample_rate,
          OutputTargetFill(output, config_.host_sample_rate));
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
  std::unique_ptr<LinearReader> input_resampler_;
  std::unique_ptr<AdaptiveOutputReader> output_resampler_;
  std::atomic<Status> status_{Status::kOff};
  std::atomic<bool> input_enabled_{false};
  std::atomic<bool> output_enabled_{false};
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
