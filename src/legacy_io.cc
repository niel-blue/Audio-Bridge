#include "legacy_io.h"

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
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <mmsystem.h>
#include <dsound.h>
#include <objbase.h>
#include <wrl/client.h>

namespace audio_bridge::audio {
namespace {

using Microsoft::WRL::ComPtr;
constexpr auto kFifoCapacity = std::size_t{1U << 16};
constexpr auto kMmeBufferCount = std::size_t{4U};

enum class LegacyBackend {
  kMmeInput,
  kMmeOutput,
  kDirectSoundInput,
  kDirectSoundOutput,
};

auto ParseLegacyId(const std::string& id, LegacyBackend& backend,
                   std::string& payload) -> bool {
  struct Prefix {
    std::string_view text;
    LegacyBackend backend;
  };
  constexpr auto prefixes = std::array{
      Prefix{"mme:input:", LegacyBackend::kMmeInput},
      Prefix{"mme:output:", LegacyBackend::kMmeOutput},
      Prefix{"dsound:input:", LegacyBackend::kDirectSoundInput},
      Prefix{"dsound:output:", LegacyBackend::kDirectSoundOutput},
  };
  for (const auto& prefix : prefixes) {
    if (id.starts_with(prefix.text) && id.size() > prefix.text.size()) {
      backend = prefix.backend;
      payload = id.substr(prefix.text.size());
      return true;
    }
  }
  return false;
}

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

auto MmeError(const char* const operation, const MMRESULT result) -> std::string {
  auto stream = std::ostringstream{};
  stream << operation << " failed (MME " << result << ").";
  return stream.str();
}

auto HResultText(const char* const operation, const HRESULT result)
    -> std::string {
  auto stream = std::ostringstream{};
  stream << operation << " failed (0x" << std::uppercase << std::hex
         << static_cast<unsigned long>(result) << ").";
  return stream.str();
}

auto MakePcmFormat(const DWORD sample_rate, const WORD channels)
    -> WAVEFORMATEX {
  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = channels;
  format.nSamplesPerSec = sample_rate;
  format.wBitsPerSample = 16;
  format.nBlockAlign = static_cast<WORD>(channels * sizeof(std::int16_t));
  format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
  return format;
}

auto CandidateRates(const double host_rate) -> std::vector<DWORD> {
  auto result = std::vector<DWORD>{};
  const auto add = [&result](const double value) {
    const auto rate = static_cast<DWORD>(std::clamp(value, 8000.0, 192000.0));
    if (std::find(result.begin(), result.end(), rate) == result.end()) {
      result.push_back(rate);
    }
  };
  add(host_rate);
  for (const auto rate : {48000.0, 44100.0, 32000.0, 96000.0, 22050.0,
                          16000.0, 8000.0}) {
    add(rate);
  }
  return result;
}

auto TryMmeFormat(const bool input, const UINT device,
                  const double host_rate, WAVEFORMATEX& format) -> bool {
  for (const auto rate : CandidateRates(host_rate)) {
    for (const auto channels : {static_cast<WORD>(2), static_cast<WORD>(1)}) {
      const auto candidate = MakePcmFormat(rate, channels);
      MMRESULT result = MMSYSERR_ERROR;
      if (input) {
        result = waveInOpen(nullptr, static_cast<UINT_PTR>(device),
                            &candidate, 0, 0, WAVE_FORMAT_QUERY);
      } else {
        result = waveOutOpen(nullptr, static_cast<UINT_PTR>(device),
                             &candidate, 0, 0, WAVE_FORMAT_QUERY);
      }
      if (result == MMSYSERR_NOERROR) {
        format = candidate;
        return true;
      }
    }
  }
  return false;
}

auto ParseMmeIndex(const std::string& payload, UINT& index) -> bool {
  try {
    std::size_t parsed = 0;
    const auto value = std::stoul(payload, &parsed, 10);
    if (parsed != payload.size() || value > UINT_MAX) {
      return false;
    }
    index = static_cast<UINT>(value);
    return true;
  } catch (...) {
    return false;
  }
}

auto ParseGuid(const std::string& payload, GUID& guid) -> bool {
  if (payload == "default") {
    return true;
  }
  const auto wide = Utf8ToWide(payload);
  return !wide.empty() && SUCCEEDED(CLSIDFromString(wide.c_str(), &guid));
}

void ReadPcm16(const BYTE* const data, const std::size_t frame,
               const WAVEFORMATEX& format, float& left, float& right) noexcept {
  const auto* const samples = reinterpret_cast<const std::int16_t*>(data);
  const auto offset = frame * format.nChannels;
  left = static_cast<float>(samples[offset]) / 32768.0F;
  right = format.nChannels > 1U
              ? static_cast<float>(samples[offset + 1U]) / 32768.0F
              : left;
}

void WritePcm16(BYTE* const data, const std::size_t frame,
                const WAVEFORMATEX& format, const float left,
                const float right) noexcept {
  auto* const samples = reinterpret_cast<std::int16_t*>(data);
  const auto mono = (left + right) * 0.5F;
  const auto offset = frame * format.nChannels;
  for (auto channel = 0U; channel < format.nChannels; ++channel) {
    const auto value = format.nChannels == 1U
                           ? mono
                           : (channel == 0U ? left : right);
    samples[offset + channel] = static_cast<std::int16_t>(std::lrint(
        std::clamp(value, -1.0F, 1.0F) * 32767.0F));
  }
}

struct MmeInput {
  HWAVEIN handle = nullptr;
  HANDLE event = nullptr;
  WAVEFORMATEX format{};
  UINT buffer_frames = 0U;
  std::vector<std::vector<BYTE>> storage;
  std::vector<WAVEHDR> headers;
};

struct MmeOutput {
  HWAVEOUT handle = nullptr;
  HANDLE event = nullptr;
  WAVEFORMATEX format{};
  UINT buffer_frames = 0U;
  std::vector<std::vector<BYTE>> storage;
  std::vector<WAVEHDR> headers;
};

struct DirectInput {
  ComPtr<IDirectSoundCapture8> device;
  ComPtr<IDirectSoundCaptureBuffer8> buffer;
  WAVEFORMATEX format{};
  DWORD buffer_bytes = 0U;
  DWORD read_position = 0U;
};

struct DirectOutput {
  ComPtr<IDirectSound8> device;
  ComPtr<IDirectSoundBuffer8> buffer;
  WAVEFORMATEX format{};
  DWORD buffer_bytes = 0U;
  DWORD write_position = 0U;
  DWORD chunk_bytes = 0U;
};

}  // namespace

class LegacyIo::Impl final {
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
    if (!InputEnabled() && !OutputEnabled()) {
      return;
    }
    input_raw_.Clear();
    input_host_.Clear();
    output_host_.Clear();
    config_ = config;
    status_.store(Status::kStarting, std::memory_order_release);
    Notify(Status::kStarting, {});
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stop_event_ == nullptr) {
      SetUnavailable("Legacy audio stop event creation failed.");
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
    if (OutputEnabled()) {
      static_cast<void>(output_host_.Push(left, right, count));
    }
  }

  void PopInputBlock(float* const left, float* const right,
                     const std::size_t count) noexcept {
    if (InputEnabled()) {
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

  void CloseMmeInput() noexcept {
    if (mme_input_.handle != nullptr) {
      static_cast<void>(waveInReset(mme_input_.handle));
      for (auto& header : mme_input_.headers) {
        if ((header.dwFlags & WHDR_PREPARED) != 0U) {
          static_cast<void>(waveInUnprepareHeader(
              mme_input_.handle, &header, sizeof(header)));
        }
      }
      static_cast<void>(waveInClose(mme_input_.handle));
      mme_input_.handle = nullptr;
    }
    if (mme_input_.event != nullptr) {
      CloseHandle(mme_input_.event);
      mme_input_.event = nullptr;
    }
    mme_input_.headers.clear();
    mme_input_.storage.clear();
  }

  void CloseMmeOutput() noexcept {
    if (mme_output_.handle != nullptr) {
      static_cast<void>(waveOutReset(mme_output_.handle));
      for (auto& header : mme_output_.headers) {
        if ((header.dwFlags & WHDR_PREPARED) != 0U) {
          static_cast<void>(waveOutUnprepareHeader(
              mme_output_.handle, &header, sizeof(header)));
        }
      }
      static_cast<void>(waveOutClose(mme_output_.handle));
      mme_output_.handle = nullptr;
    }
    if (mme_output_.event != nullptr) {
      CloseHandle(mme_output_.event);
      mme_output_.event = nullptr;
    }
    mme_output_.headers.clear();
    mme_output_.storage.clear();
  }

  auto OpenMmeInput(const std::string& payload) -> std::string {
    UINT index = 0U;
    if (!ParseMmeIndex(payload, index)) {
      return "Invalid MME input device identifier.";
    }
    if (!TryMmeFormat(true, index, config_.host_sample_rate,
                      mme_input_.format)) {
      return "The selected MME input device does not support a compatible PCM format.";
    }
    mme_input_.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (mme_input_.event == nullptr) {
      return "MME input event creation failed.";
    }
    auto result = waveInOpen(
        &mme_input_.handle, static_cast<UINT_PTR>(index), &mme_input_.format,
        reinterpret_cast<DWORD_PTR>(mme_input_.event), 0, CALLBACK_EVENT);
    if (result != MMSYSERR_NOERROR) {
      return MmeError("MME input open", result);
    }
    mme_input_.buffer_frames = std::clamp<UINT>(
        mme_input_.format.nSamplesPerSec / 100U, 256U, 2048U);
    const auto bytes = static_cast<std::size_t>(mme_input_.buffer_frames) *
                       mme_input_.format.nBlockAlign;
    mme_input_.storage.resize(kMmeBufferCount);
    mme_input_.headers.resize(kMmeBufferCount);
    for (std::size_t index = 0; index < kMmeBufferCount; ++index) {
      mme_input_.storage[index].assign(bytes, 0U);
      auto& header = mme_input_.headers[index];
      std::memset(&header, 0, sizeof(header));
      header.lpData = reinterpret_cast<LPSTR>(mme_input_.storage[index].data());
      header.dwBufferLength = static_cast<DWORD>(bytes);
      result = waveInPrepareHeader(mme_input_.handle, &header, sizeof(header));
      if (result != MMSYSERR_NOERROR) {
        return MmeError("MME input buffer preparation", result);
      }
      result = waveInAddBuffer(mme_input_.handle, &header, sizeof(header));
      if (result != MMSYSERR_NOERROR) {
        return MmeError("MME input buffer queue", result);
      }
    }
    return {};
  }

  auto OpenMmeOutput(const std::string& payload) -> std::string {
    UINT index = 0U;
    if (!ParseMmeIndex(payload, index)) {
      return "Invalid MME output device identifier.";
    }
    if (!TryMmeFormat(false, index, config_.host_sample_rate,
                      mme_output_.format)) {
      return "The selected MME output device does not support a compatible PCM format.";
    }
    mme_output_.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (mme_output_.event == nullptr) {
      return "MME output event creation failed.";
    }
    auto result = waveOutOpen(
        &mme_output_.handle, static_cast<UINT_PTR>(index),
        &mme_output_.format, reinterpret_cast<DWORD_PTR>(mme_output_.event), 0,
        CALLBACK_EVENT);
    if (result != MMSYSERR_NOERROR) {
      return MmeError("MME output open", result);
    }
    mme_output_.buffer_frames = std::clamp<UINT>(
        mme_output_.format.nSamplesPerSec / 100U, 256U, 2048U);
    const auto bytes = static_cast<std::size_t>(mme_output_.buffer_frames) *
                       mme_output_.format.nBlockAlign;
    mme_output_.storage.resize(kMmeBufferCount);
    mme_output_.headers.resize(kMmeBufferCount);
    for (std::size_t buffer = 0; buffer < kMmeBufferCount; ++buffer) {
      mme_output_.storage[buffer].assign(bytes, 0U);
      auto& header = mme_output_.headers[buffer];
      std::memset(&header, 0, sizeof(header));
      header.lpData = reinterpret_cast<LPSTR>(mme_output_.storage[buffer].data());
      header.dwBufferLength = static_cast<DWORD>(bytes);
      result = waveOutPrepareHeader(mme_output_.handle, &header, sizeof(header));
      if (result != MMSYSERR_NOERROR) {
        return MmeError("MME output buffer preparation", result);
      }
    }
    return {};
  }

  auto OpenDirectInput(const std::string& payload) -> std::string {
    GUID guid{};
    if (!ParseGuid(payload, guid)) {
      return "Invalid DirectSound input device identifier.";
    }
    const LPGUID guid_ptr = payload == "default" ? nullptr : &guid;
    auto result = DirectSoundCaptureCreate8(guid_ptr, &direct_input_.device,
                                            nullptr);
    if (FAILED(result)) {
      return HResultText("DirectSound capture device open", result);
    }
    for (const auto rate : CandidateRates(config_.host_sample_rate)) {
      for (const auto channels : {static_cast<WORD>(2), static_cast<WORD>(1)}) {
        const auto format = MakePcmFormat(rate, channels);
        DSCBUFFERDESC description{};
        description.dwSize = sizeof(description);
        description.dwBufferBytes = std::max<DWORD>(
            format.nBlockAlign * 4U,
            (format.nAvgBytesPerSec / 5U / format.nBlockAlign) *
                format.nBlockAlign);
        description.lpwfxFormat = const_cast<WAVEFORMATEX*>(&format);
        ComPtr<IDirectSoundCaptureBuffer> base_buffer;
        result = direct_input_.device->CreateCaptureBuffer(
            &description, &base_buffer, nullptr);
        if (FAILED(result)) {
          continue;
        }
        result = base_buffer->QueryInterface(
            IID_IDirectSoundCaptureBuffer8,
            reinterpret_cast<void**>(direct_input_.buffer.GetAddressOf()));
        if (SUCCEEDED(result)) {
          direct_input_.format = format;
          direct_input_.buffer_bytes = description.dwBufferBytes;
          return {};
        }
      }
    }
    return "The selected DirectSound input device does not support a compatible PCM format.";
  }

  auto OpenDirectOutput(const std::string& payload) -> std::string {
    GUID guid{};
    if (!ParseGuid(payload, guid)) {
      return "Invalid DirectSound output device identifier.";
    }
    const LPGUID guid_ptr = payload == "default" ? nullptr : &guid;
    auto result = DirectSoundCreate8(guid_ptr, &direct_output_.device, nullptr);
    if (FAILED(result)) {
      return HResultText("DirectSound output device open", result);
    }
    result = direct_output_.device->SetCooperativeLevel(GetDesktopWindow(),
                                                        DSSCL_NORMAL);
    if (FAILED(result)) {
      return HResultText("DirectSound cooperative level", result);
    }
    for (const auto rate : CandidateRates(config_.host_sample_rate)) {
      for (const auto channels : {static_cast<WORD>(2), static_cast<WORD>(1)}) {
        const auto format = MakePcmFormat(rate, channels);
        DSBUFFERDESC description{};
        description.dwSize = sizeof(description);
        description.dwFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2;
        description.dwBufferBytes = std::max<DWORD>(
            format.nBlockAlign * 8U,
            (format.nAvgBytesPerSec / 4U / format.nBlockAlign) *
                format.nBlockAlign);
        description.lpwfxFormat = const_cast<WAVEFORMATEX*>(&format);
        ComPtr<IDirectSoundBuffer> base_buffer;
        result = direct_output_.device->CreateSoundBuffer(
            &description, &base_buffer, nullptr);
        if (FAILED(result)) {
          continue;
        }
        result = base_buffer->QueryInterface(
            IID_IDirectSoundBuffer8,
            reinterpret_cast<void**>(direct_output_.buffer.GetAddressOf()));
        if (SUCCEEDED(result)) {
          direct_output_.format = format;
          direct_output_.buffer_bytes = description.dwBufferBytes;
          direct_output_.chunk_bytes = std::max<DWORD>(
              format.nBlockAlign,
              (format.nAvgBytesPerSec / 100U / format.nBlockAlign) *
                  format.nBlockAlign);
          return {};
        }
      }
    }
    return "The selected DirectSound output device does not support a compatible PCM format.";
  }

  void ProcessMmeInput() {
    for (auto& header : mme_input_.headers) {
      if ((header.dwFlags & WHDR_DONE) == 0U) {
        continue;
      }
      const auto frames = header.dwBytesRecorded / mme_input_.format.nBlockAlign;
      const auto* const data = reinterpret_cast<const BYTE*>(header.lpData);
      for (DWORD frame = 0; frame < frames; ++frame) {
        float left = 0.0F;
        float right = 0.0F;
        ReadPcm16(data, frame, mme_input_.format, left, right);
        static_cast<void>(input_raw_.Push(&left, &right, 1U));
      }
      static_cast<void>(waveInUnprepareHeader(mme_input_.handle, &header,
                                              sizeof(header)));
      header.dwBytesRecorded = 0U;
      header.dwFlags = 0U;
      if (waveInPrepareHeader(mme_input_.handle, &header, sizeof(header)) ==
          MMSYSERR_NOERROR) {
        static_cast<void>(waveInAddBuffer(mme_input_.handle, &header,
                                           sizeof(header)));
      }
    }
    ConvertInput();
  }

  void ProcessMmeOutput() {
    for (auto& header : mme_output_.headers) {
      if ((header.dwFlags & WHDR_DONE) == 0U) {
        continue;
      }
      static_cast<void>(waveOutUnprepareHeader(mme_output_.handle, &header,
                                                sizeof(header)));
      header.dwFlags = 0U;
      if (waveOutPrepareHeader(mme_output_.handle, &header, sizeof(header)) ==
          MMSYSERR_NOERROR) {
        FillPcmBuffer(reinterpret_cast<BYTE*>(header.lpData),
                      mme_output_.buffer_frames, mme_output_.format);
        static_cast<void>(waveOutWrite(mme_output_.handle, &header,
                                       sizeof(header)));
      }
    }
  }

  void ConsumePcmBytes(const BYTE* const data, const DWORD bytes,
                       const WAVEFORMATEX& format) {
    if (data == nullptr || format.nBlockAlign == 0U) {
      return;
    }
    const auto frames = bytes / format.nBlockAlign;
    for (DWORD frame = 0; frame < frames; ++frame) {
      float left = 0.0F;
      float right = 0.0F;
      ReadPcm16(data, frame, format, left, right);
      static_cast<void>(input_raw_.Push(&left, &right, 1U));
    }
  }

  void ProcessDirectInput() {
    if (direct_input_.buffer == nullptr || direct_input_.buffer_bytes == 0U) {
      return;
    }
    DWORD capture_position = 0U;
    DWORD read_position = 0U;
    if (FAILED(direct_input_.buffer->GetCurrentPosition(&capture_position,
                                                        &read_position))) {
      return;
    }
    const auto available = capture_position >= direct_input_.read_position
                               ? capture_position - direct_input_.read_position
                               : direct_input_.buffer_bytes -
                                     direct_input_.read_position +
                                     capture_position;
    auto bytes = std::min<DWORD>(available, direct_input_.buffer_bytes);
    bytes -= bytes % direct_input_.format.nBlockAlign;
    if (bytes == 0U) {
      return;
    }

    void* first = nullptr;
    DWORD first_bytes = 0U;
    void* second = nullptr;
    DWORD second_bytes = 0U;
    if (FAILED(direct_input_.buffer->Lock(
            direct_input_.read_position, bytes, &first, &first_bytes, &second,
            &second_bytes, 0U))) {
      return;
    }
    ConsumePcmBytes(static_cast<const BYTE*>(first), first_bytes,
                    direct_input_.format);
    ConsumePcmBytes(static_cast<const BYTE*>(second), second_bytes,
                    direct_input_.format);
    static_cast<void>(direct_input_.buffer->Unlock(first, first_bytes, second,
                                                   second_bytes));
    direct_input_.read_position =
        (direct_input_.read_position + bytes) % direct_input_.buffer_bytes;
    ConvertInput();
  }

  void FillPcmBuffer(BYTE* const data, const DWORD frames,
                     const WAVEFORMATEX& format) {
    for (DWORD frame = 0; frame < frames; ++frame) {
      float left = 0.0F;
      float right = 0.0F;
      if (output_resampler_ != nullptr) {
        output_resampler_->Next(left, right);
      }
      WritePcm16(data, frame, format, left, right);
    }
  }

  void ProcessDirectOutput() {
    if (direct_output_.buffer == nullptr || direct_output_.buffer_bytes == 0U) {
      return;
    }
    DWORD play_position = 0U;
    DWORD write_position = 0U;
    if (FAILED(direct_output_.buffer->GetCurrentPosition(&play_position,
                                                         &write_position))) {
      return;
    }
    const auto available = play_position >= direct_output_.write_position
                               ? play_position - direct_output_.write_position
                               : direct_output_.buffer_bytes -
                                     direct_output_.write_position +
                                     play_position;
    auto bytes = std::min<DWORD>(available, direct_output_.chunk_bytes);
    bytes -= bytes % direct_output_.format.nBlockAlign;
    if (bytes == 0U) {
      return;
    }
    void* first = nullptr;
    DWORD first_bytes = 0U;
    void* second = nullptr;
    DWORD second_bytes = 0U;
    if (FAILED(direct_output_.buffer->Lock(
            direct_output_.write_position, bytes, &first, &first_bytes,
            &second, &second_bytes, 0U))) {
      return;
    }
    FillPcmBuffer(static_cast<BYTE*>(first),
                  first_bytes / direct_output_.format.nBlockAlign,
                  direct_output_.format);
    FillPcmBuffer(static_cast<BYTE*>(second),
                  second_bytes / direct_output_.format.nBlockAlign,
                  direct_output_.format);
    static_cast<void>(direct_output_.buffer->Unlock(first, first_bytes,
                                                    second, second_bytes));
    direct_output_.write_position =
        (direct_output_.write_position + bytes) % direct_output_.buffer_bytes;
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

  auto QueueMmeOutput(MmeOutput& output) -> std::string {
    for (auto& header : output.headers) {
      if ((header.dwFlags & WHDR_INQUEUE) != 0U) {
        continue;
      }
      FillPcmBuffer(reinterpret_cast<BYTE*>(header.lpData),
                    output.buffer_frames, output.format);
      const auto result = waveOutWrite(output.handle, &header, sizeof(header));
      if (result != MMSYSERR_NOERROR) {
        return MmeError("MME output buffer queue", result);
      }
    }
    return {};
  }

  void StartDirectInput() {
    DWORD capture_position = 0U;
    DWORD read_position = 0U;
    static_cast<void>(direct_input_.buffer->GetCurrentPosition(
        &capture_position, &read_position));
    direct_input_.read_position = capture_position;
    static_cast<void>(direct_input_.buffer->Start(DSCBSTART_LOOPING));
  }

  auto StartDirectOutput() -> std::string {
    void* first = nullptr;
    DWORD first_bytes = 0U;
    void* second = nullptr;
    DWORD second_bytes = 0U;
    auto result = direct_output_.buffer->Lock(
        0U, direct_output_.buffer_bytes, &first, &first_bytes, &second,
        &second_bytes, DSBLOCK_ENTIREBUFFER);
    if (FAILED(result)) {
      return HResultText("DirectSound output buffer lock", result);
    }
    if (first != nullptr) {
      std::memset(first, 0, first_bytes);
    }
    if (second != nullptr) {
      std::memset(second, 0, second_bytes);
    }
    static_cast<void>(direct_output_.buffer->Unlock(first, first_bytes, second,
                                                    second_bytes));
    direct_output_.write_position = 0U;
    result = direct_output_.buffer->Play(0U, 0U, DSBPLAY_LOOPING);
    return FAILED(result) ? HResultText("DirectSound output start", result)
                          : std::string{};
  }

  void StopStreams() noexcept {
    if (mme_input_.handle != nullptr) {
      static_cast<void>(waveInStop(mme_input_.handle));
    }
    if (mme_output_.handle != nullptr) {
      static_cast<void>(waveOutReset(mme_output_.handle));
    }
    if (direct_input_.buffer != nullptr) {
      static_cast<void>(direct_input_.buffer->Stop());
    }
    if (direct_output_.buffer != nullptr) {
      static_cast<void>(direct_output_.buffer->Stop());
    }
  }

  void Run() {
    const auto com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const auto uninitialize = SUCCEEDED(com_result);
    if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE) {
      SetUnavailable("Legacy audio COM initialization failed.");
      return;
    }

    LegacyBackend input_backend{};
    LegacyBackend output_backend{};
    auto input_payload = std::string{};
    auto output_payload = std::string{};
    auto error = std::string{};
    if (InputEnabled() &&
        !ParseLegacyId(config_.input_id, input_backend, input_payload)) {
      error = "The selected input device uses an unsupported audio backend.";
    }
    if (error.empty() && OutputEnabled() &&
        !ParseLegacyId(config_.output_id, output_backend, output_payload)) {
      error = "The selected output device uses an unsupported audio backend.";
    }
    if (error.empty() && InputEnabled()) {
      switch (input_backend) {
        case LegacyBackend::kMmeInput:
          error = OpenMmeInput(input_payload);
          break;
        case LegacyBackend::kDirectSoundInput:
          error = OpenDirectInput(input_payload);
          break;
        default:
          error = "The selected input device is not an input device.";
          break;
      }
    }
    if (error.empty() && OutputEnabled()) {
      switch (output_backend) {
        case LegacyBackend::kMmeOutput:
          error = OpenMmeOutput(output_payload);
          break;
        case LegacyBackend::kDirectSoundOutput:
          error = OpenDirectOutput(output_payload);
          break;
        default:
          error = "The selected output device is not an output device.";
          break;
      }
    }
    if (error.empty() && InputEnabled()) {
      const auto source_rate = input_backend == LegacyBackend::kMmeInput
                                   ? mme_input_.format.nSamplesPerSec
                                   : direct_input_.format.nSamplesPerSec;
      input_resampler_ = std::make_unique<LinearReader>(
          input_raw_, source_rate, config_.host_sample_rate);
    }
    if (error.empty() && OutputEnabled()) {
      const auto destination_rate = output_backend == LegacyBackend::kMmeOutput
                                        ? mme_output_.format.nSamplesPerSec
                                        : direct_output_.format.nSamplesPerSec;
      output_resampler_ = std::make_unique<LinearReader>(
          output_host_, config_.host_sample_rate, destination_rate);
    }
    if (error.empty() && mme_input_.handle != nullptr) {
      const auto result = waveInStart(mme_input_.handle);
      if (result != MMSYSERR_NOERROR) {
        error = MmeError("MME input start", result);
      }
    }
    if (error.empty() && mme_output_.handle != nullptr) {
      error = QueueMmeOutput(mme_output_);
    }
    if (error.empty() && direct_input_.buffer != nullptr) {
      StartDirectInput();
    }
    if (error.empty() && direct_output_.buffer != nullptr) {
      error = StartDirectOutput();
    }
    if (!error.empty()) {
      SetUnavailable(error);
      StopStreams();
      CloseMmeInput();
      CloseMmeOutput();
      input_resampler_.reset();
      output_resampler_.reset();
      if (uninitialize) {
        CoUninitialize();
      }
      return;
    }

    status_.store(Status::kActive, std::memory_order_release);
    Notify(Status::kActive, {});

    auto handles = std::array<HANDLE, 3>{};
    DWORD handle_count = 1U;
    handles[0] = stop_event_;
    DWORD input_handle_index = UINT_MAX;
    DWORD output_handle_index = UINT_MAX;
    if (mme_input_.event != nullptr) {
      input_handle_index = handle_count;
      handles[handle_count++] = mme_input_.event;
    }
    if (mme_output_.event != nullptr) {
      output_handle_index = handle_count;
      handles[handle_count++] = mme_output_.event;
    }

    auto running = true;
    while (running) {
      const auto wait_result = WaitForMultipleObjects(handle_count,
                                                      handles.data(), FALSE, 5);
      if (wait_result == WAIT_OBJECT_0) {
        running = false;
        continue;
      }
      if (wait_result == WAIT_OBJECT_0 + input_handle_index) {
        ProcessMmeInput();
      }
      if (wait_result == WAIT_OBJECT_0 + output_handle_index) {
        ProcessMmeOutput();
      }
      ProcessDirectInput();
      ProcessDirectOutput();
    }

    StopStreams();
    CloseMmeInput();
    CloseMmeOutput();
    direct_input_.buffer.Reset();
    direct_input_.device.Reset();
    direct_output_.buffer.Reset();
    direct_output_.device.Reset();
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
  MmeInput mme_input_;
  MmeOutput mme_output_;
  DirectInput direct_input_;
  DirectOutput direct_output_;
  std::unique_ptr<LinearReader> input_resampler_;
  std::unique_ptr<LinearReader> output_resampler_;
  std::atomic<Status> status_{Status::kOff};
  std::atomic<bool> input_enabled_{false};
  std::atomic<bool> output_enabled_{false};
  HANDLE stop_event_ = nullptr;
  std::thread thread_;
};

LegacyIo::LegacyIo(StatusCallback callback)
    : impl_(std::make_unique<Impl>(std::move(callback))) {}
LegacyIo::~LegacyIo() = default;
void LegacyIo::Start(const Config& config) { impl_->Start(config); }
void LegacyIo::Stop() noexcept { impl_->Stop(); }
void LegacyIo::PushOutputBlock(const float* left, const float* right,
                               const std::size_t count) noexcept {
  impl_->PushOutputBlock(left, right, count);
}
void LegacyIo::PopInputBlock(float* left, float* right,
                             const std::size_t count) noexcept {
  impl_->PopInputBlock(left, right, count);
}
auto LegacyIo::InputEnabled() const noexcept -> bool {
  return impl_->InputEnabled();
}
auto LegacyIo::OutputEnabled() const noexcept -> bool {
  return impl_->OutputEnabled();
}
auto LegacyIo::GetStatus() const noexcept -> Status {
  return impl_->GetStatus();
}

}  // namespace audio_bridge::audio

#else

namespace audio_bridge::audio {

class LegacyIo::Impl {
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

LegacyIo::LegacyIo(StatusCallback callback)
    : impl_(std::make_unique<Impl>(std::move(callback))) {}
LegacyIo::~LegacyIo() = default;
void LegacyIo::Start(const Config& config) { impl_->Start(config); }
void LegacyIo::Stop() noexcept { impl_->Stop(); }
void LegacyIo::PushOutputBlock(const float* left, const float* right,
                               const std::size_t count) noexcept {
  impl_->PushOutputBlock(left, right, count);
}
void LegacyIo::PopInputBlock(float* left, float* right,
                             const std::size_t count) noexcept {
  impl_->PopInputBlock(left, right, count);
}
auto LegacyIo::InputEnabled() const noexcept -> bool {
  return impl_->InputEnabled();
}
auto LegacyIo::OutputEnabled() const noexcept -> bool {
  return impl_->OutputEnabled();
}
auto LegacyIo::GetStatus() const noexcept -> Status { return impl_->GetStatus(); }

}  // namespace audio_bridge::audio

#endif
